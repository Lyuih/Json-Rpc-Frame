#pragma once
/**
 * 封装服务器
 */
#include "../common/dispatcher.hpp"
#include "router.hpp"
#include "topic.hpp"
#include "registry.hpp"
#include "../client/client.hpp"

namespace Lyuih
{
    namespace server
    {
        class RpcServer
        {
        public:
            using ptr = std::shared_ptr<RpcServer>;
            RpcServer(const Address &access_addr, bool enableRegistry = false, const Address &registry_server_addr = Address())
                : enableRegistry_(enableRegistry), access_addr_(access_addr), router_(std::make_shared<RpcRouter>()), dispatcher_(std::make_shared<Dispatcher>())
            {
                if (enableRegistry)
                {
                    reg_client_ = std::make_shared<client::RegistryClient>(registry_server_addr.first, registry_server_addr.second);
                }
                auto rpc_cb = std::bind(&RpcRouter::onRpcRequest, router_.get(),
                                        std::placeholders::_1, std::placeholders::_2);
                dispatcher_->registerHandler<RpcRequest>(MType::REQ_RPC, rpc_cb);

                server_ = ServerFactory::create(access_addr.second);
                auto message_cb = std::bind(&Dispatcher::onMesssage, dispatcher_.get(),
                                            std::placeholders::_1, std::placeholders::_2);
                server_->setMessageCallback(message_cb);
            }
            void registerMethod(const ServerDescribe::ptr &service)
            {
                if (enableRegistry_)
                {
                    reg_client_->registryMethod(service->method(), access_addr_);
                }
                router_->resisterMethod(service);
            }
            void start()
            {
                server_->start();
            }

        private:
            bool enableRegistry_;
            Address access_addr_;
            client::RegistryClient::ptr reg_client_;
            RpcRouter::ptr router_;
            Dispatcher::ptr dispatcher_;
            BaseServer::ptr server_;
        };
        class TopicServer
        {
        public:
            using ptr = std::shared_ptr<TopicServer>;
            TopicServer(int port)
                : topic_manager_(std::make_shared<TopicManager>()), dispatcher_(std::make_shared<Dispatcher>())
            {
                auto topic_cb = std::bind(&TopicManager::onTopicRequest, topic_manager_.get(), std::placeholders::_1, std::placeholders::_2);
                // 注册到消息分发
                dispatcher_->registerHandler<TopicRequest>(MType::REQ_TOPIC, topic_cb);
                server_ = ServerFactory::create(port);
                auto message_cb = std::bind(&Dispatcher::onMesssage, dispatcher_.get(), std::placeholders::_1, std::placeholders::_2);
                server_->setMessageCallback(message_cb);
                auto close_cb = std::bind(&TopicServer::onConnShutdown, this, std::placeholders::_1);
                server_->setCloseback(close_cb);
            }
            void start()
            {
                server_->start();
            }

        private:
            void onConnShutdown(const BaseConnection::ptr &conn)
            {
                topic_manager_->onShutdown(conn);
            }

        private:
            TopicManager::ptr topic_manager_;
            Dispatcher::ptr dispatcher_; // 消息分发器，用于分发和处理不同类型的消息请求
            BaseServer::ptr server_;     // 网络服务器基础对象，负责监听和管理客户端连接
        };
        // 注册中心服务器，只需要针对服务注册与发现请求进行处理
        class RegistryServer
        {
        public:
            using ptr = std::shared_ptr<RegistryServer>;
            RegistryServer(int port)
                : pd_manager_(std::make_shared<PDManager>()), dispatcher_(std::make_shared<Dispatcher>())
            {
                auto service_cb = std::bind(&PDManager::onServiceRequest, pd_manager_.get(), std::placeholders::_1, std::placeholders::_2);
                // 注册到消息分发
                dispatcher_->registerHandler<ServiceRequest>(MType::REQ_SERVICE, service_cb);
                server_ = ServerFactory::create(port);
                auto message_cb = std::bind(&Dispatcher::onMesssage, dispatcher_.get(), std::placeholders::_1, std::placeholders::_2);
                server_->setMessageCallback(message_cb);
                auto close_cb = std::bind(&RegistryServer::onConnShutdown, this, std::placeholders::_1);
                server_->setCloseback(close_cb);
            }
            void start()
            {
                server_->start();
            }

        private:
            void onConnShutdown(const BaseConnection::ptr &conn)
            {
                pd_manager_->onConnShutdown(conn);
            }

        private:
            PDManager::ptr pd_manager_;  // 服务注册与发现协调管理器
            Dispatcher::ptr dispatcher_; // 消息分发器，用于分发和处理不同类型的消息请求
            BaseServer::ptr server_;     // 网络服务器基础对象，负责监听和管理客户端连接
        };

    };

};