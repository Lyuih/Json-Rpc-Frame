#pragma once
/**
 * 实现 Json-RPC 客户端的服务注册、服务发现、远程过程调用（RPC）和主题（Topic）管理功能。
 */
#include "../common/dispatcher.hpp"
#include "caller.hpp"
#include "registry.hpp"
#include "topic.hpp"

namespace Lyuih
{
    namespace client
    {
        // 向注册中心注册服务提供者的方法和地址。
        class RegistryClient
        {
        public:
            using ptr = std::shared_ptr<RegistryClient>;
            // 构造函数传入注册中心的地址信息，用于连接注册中心
            RegistryClient(const std::string &ip, const int port)
                : requestor_(std::make_shared<Requestor>()), provider_(std::make_shared<Provider>(requestor_)), dispatcher_(std::make_shared<Dispatcher>())
            {
                // 注册回调函数
                auto rsp_cb = std::bind(&Requestor::onResponse, requestor_.get(), std::placeholders::_1, std::placeholders::_2);
                dispatcher_->registerHandler<BaseMessage>(MType::RSP_SERVICE, rsp_cb);
                auto message_cb = std::bind(&Dispatcher::onMesssage, dispatcher_.get(), std::placeholders::_1, std::placeholders::_2);

                client_ = ClientFactory::create(ip, port);
                client_->setMessageCallback(message_cb);
                client_->connect();
            }
            // 提供服务注册接口,调用 Provider::registryMethod 注册服务方法和地址。
            bool registryMethod(const std::string &method, const Address &host)
            {
                return provider_->registryMethod(client_->connection(), method, host);
            }

        private:
            Requestor::ptr requestor_;
            Provider::ptr provider_;
            Dispatcher::ptr dispatcher_;
            BaseClient::ptr client_;
        };
        class DiscoveryClient
        {
        public:
            using ptr = std::shared_ptr<DiscoveryClient>;
            // 构造函数传入注册中心的地址信息，用于连接注册中心,
            DiscoveryClient(const std::string &ip, const int port, const Discoverer::OfflineCallback &cb)
                : requestor_(std::make_shared<Requestor>()), discover_(std::make_shared<Discoverer>(requestor_, cb)), dispatcher_(std::make_shared<Dispatcher>())
            {
                auto rsp_cb = std::bind(&Requestor::onResponse, requestor_.get(), std::placeholders::_1, std::placeholders::_2);
                dispatcher_->registerHandler<BaseMessage>(MType::RSP_SERVICE, rsp_cb);
                auto req_cb = std::bind(&Discoverer::onServiceRequest, discover_.get(), std::placeholders::_1, std::placeholders::_2);
                dispatcher_->registerHandler<ServiceRequest>(MType::REQ_SERVICE, req_cb);
                auto message_cb = std::bind(&Dispatcher::onMesssage, dispatcher_.get(), std::placeholders::_1, std::placeholders::_2);
                client_ = ClientFactory::create(ip, port);
                client_->setMessageCallback(message_cb);
                client_->connect();
            }
            bool serviceDiscovery(const std::string &method, Address &host)
            {
                return discover_->serviceDiscovery(client_->connection(), method, host);
            }

        private:
            Requestor::ptr requestor_;
            Discoverer::ptr discover_;
            Dispatcher::ptr dispatcher_;
            BaseClient::ptr client_;
        };
        class TopicClient
        {
        public:
            using ptr = std::shared_ptr<TopicClient>;
            // 初始化主题客户端，连接服务器，注册响应和推送回调。
            TopicClient(const std::string &ip, const int port)
                : requestor_(std::make_shared<Requestor>()),
                  topic_manager_(std::make_shared<TopicManager>(requestor_)),
                  dispatcher_(std::make_shared<Dispatcher>())
            {
                auto rsp_cb = std::bind(&Requestor::onResponse, requestor_.get(), std::placeholders::_1, std::placeholders::_2);
                dispatcher_->registerHandler<BaseMessage>(MType::RSP_TOPIC, rsp_cb);
                auto msg_cb = std::bind(&TopicManager::onPublish, topic_manager_.get(), std::placeholders::_1, std::placeholders::_2);
                dispatcher_->registerHandler<TopicRequest>(MType::REQ_TOPIC, msg_cb);
                auto message_cb = std::bind(&Dispatcher::onMesssage, dispatcher_.get(), std::placeholders::_1, std::placeholders::_2);
                client_ = ClientFactory::create(ip, port);
                client_->setMessageCallback(message_cb);
                client_->connect();
            }
            bool create(const std::string &topic_key)
            {
                return topic_manager_->create(client_->connection(), topic_key);
            }
            bool cancel(const std::string &topic_key)
            {
                return topic_manager_->cancel(client_->connection(), topic_key);
            }
            bool remove(const std::string &topic_key)
            {
                return topic_manager_->remove(client_->connection(), topic_key);
            }
            bool subscribe(const std::string &topic_key, const TopicManager::SubCallback &cb)
            {
                return topic_manager_->subscribe(client_->connection(), topic_key, cb);
            }
            bool publish(const std::string &topic_key, const std::string &msg)
            {
                return topic_manager_->publish(client_->connection(), topic_key, msg);
            }
            void shutdown()
            {
                client_->shutdown();
            }

        private:
            Requestor::ptr requestor_;
            TopicManager::ptr topic_manager_;
            Dispatcher::ptr dispatcher_;
            BaseClient::ptr client_;
        };
        // 执行 RPC 调用，支持服务发现或直接连接服务提供者。
        class RpcClient
        {
        public:
            using ptr = std::shared_ptr<RpcClient>;
            RpcClient(bool enableDiscovery, const std::string &ip, int port)
                : enableDiscovery_(enableDiscovery),
                  requestor_(std::make_shared<Requestor>()),
                  dispatcher_(std::make_shared<Dispatcher>()),
                  caller_(std::make_shared<RpcCaller>(requestor_))
            {
                // 针对rpc请求后的响应进行的回调处理
                auto rsp_cb = std::bind(&Requestor::onResponse, requestor_.get(), std::placeholders::_1, std::placeholders::_2);
                dispatcher_->registerHandler<BaseMessage>(MType::RSP_RPC, rsp_cb);

                // 如果启用了服务发现，地址信息是注册中心的地址，是服务发现客户端需要连接的地址，则通过地址信息实例化discovery_client
                // 如果没有启用服务发现，则地址信息是服务提供者的地址，则直接实例化好rpc_client

                if (enableDiscovery_)
                {
                    auto offline_cb = std::bind(&RpcClient::delClient, this, std::placeholders::_1);
                    discovery_client_ = std::make_shared<DiscoveryClient>(ip, port, offline_cb);
                }
                else
                {
                    auto message_cb = std::bind(&Dispatcher::onMesssage, dispatcher_.get(), std::placeholders::_1, std::placeholders::_2);
                    rpc_client_ = ClientFactory::create(ip, port);
                    rpc_client_->setMessageCallback(message_cb);
                    rpc_client_->connect();
                }
            }

            bool call(const std::string &method, const Json::Value &params, Json::Value &result)
            {
                // 获取服务提供者
                BaseClient::ptr client = getClient(method);
                if (client.get() == nullptr)
                {
                    return false;
                }
                // 通过客户端发送rpc请求
                return caller_->call(client->connection(), method, params, result);
            }
            bool call(const std::string &method, const Json::Value &params, RpcCaller::JsonAsyncResponse &result)
            {
                BaseClient::ptr client = getClient(method);
                if (client.get() == nullptr)
                {
                    return false;
                }
                return caller_->call(client->connection(), method, params, result);
            }
            bool call(const std::string &method, const Json::Value &params, const RpcCaller::JsonResponseCallback &cb)
            {
                BaseClient::ptr client = getClient(method);
                if (client.get() == nullptr)
                {
                    return false;
                }
                return caller_->call(client->connection(), method, params, cb);
            }

        private:
            BaseClient::ptr newClient(const Address &host)
            {
                auto message_cb = std::bind(&Dispatcher::onMesssage, dispatcher_.get(), std::placeholders::_1, std::placeholders::_2);
                auto client = ClientFactory::create(host.first, host.second);
                client->setMessageCallback(message_cb);
                client->connect();
                putClient(host, client);
                return client;
            }
            BaseClient::ptr getClient(const Address &host)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = rpc_clients_.find(host);
                if (it == rpc_clients_.end())
                {
                    return BaseClient::ptr();
                }
                return it->second;
            }
            BaseClient::ptr getClient(const std::string &method)
            {
                BaseClient::ptr client;
                if (enableDiscovery_)
                {
                    // 1.通过服务发现,获取服务提供者的信息
                    Address host;
                    bool ret = discovery_client_->serviceDiscovery(method, host);
                    if (ret == false)
                    {
                        LOG_ERROR("当前{}服务,没有找到服务提供者", method);
                        return BaseClient::ptr();
                    }
                    // 2.查看服务提供者是否已有实例化客户端,有则直接使用
                    client = getClient(host);
                    if (client.get() == nullptr)
                    {
                        // 没有实例则创建
                        client = newClient(host);
                    }
                }
                else
                {
                    client = rpc_client_;
                }
                return client;
            }
            void putClient(const Address &host, BaseClient::ptr &client)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                rpc_clients_.insert_or_assign(host, client);
            }
            void delClient(const Address &host)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                rpc_clients_.erase(host);
            }

        private:
            struct AddressHash
            {
                size_t operator()(const Address &host) const
                {
                    std::string addr = host.first + std::to_string(host.second);
                    return std::hash<std::string>{}(addr);
                }
            };

            bool enableDiscovery_;
            DiscoveryClient::ptr discovery_client_;

            Requestor::ptr requestor_;
            Dispatcher::ptr dispatcher_;

            RpcCaller::ptr caller_;
            BaseClient::ptr rpc_client_; // 用于未启用服务发现
            std::mutex mutex_;
            //<"127.0.0.1:8080", client1>
            std::unordered_map<Address, BaseClient::ptr, AddressHash> rpc_clients_; // 用于服务发现的客户端连接池
        };
    };
};
