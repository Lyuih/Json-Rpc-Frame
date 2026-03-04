#pragma once
/**
 * 实现一个服务注册与发现，用于管理Json-rpc服务提供者和发现者，主要功能包括
 * ProviderManager：管理服务提供者，跟踪每个连接提供的服务方法，维护方法到提供者的映射
 * DiscoverManager：管理服务发现者，跟踪客户端关注的服务方法，并在服务上线/下线时通知
 * PDManager：协调ProviderManager和DiscovererManager，处理服务注册、发现请求和断开事件，发送通知和响应。
 *
 */

#include "../common/net.hpp"
#include "../common/message.hpp"
#include "../common/uuidUtil.hpp"
#include <set>

namespace Lyuih
{
    namespace server
    {
        // 管理服务提供者，跟踪每个连接提供的服务方法，维护方法到提供者的映射
        class ProviderManager
        {
        public:
            using ptr = std::shared_ptr<ProviderManager>;
            // 描述服务提供者
            struct Provider
            {
                using ptr = std::shared_ptr<Provider>;
                std::mutex mutex_;
                BaseConnection::ptr conn_;
                Address host_;
                std::set<std::string> methods_;
                Provider(const BaseConnection::ptr &conn, const Address &host)
                    : conn_(conn), host_(host)
                {
                }
                void appendMethod(const std::string &name)
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    methods_.insert(name);
                }
            };

            // 注册服务提供者
            void appendProvider(const BaseConnection::ptr &conn, const Address &addr, const std::string &method)
            {
                Provider::ptr provider;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    // 1.查找该服务提供者是否已经存在
                    auto it = conns_.find(conn);
                    if (it == conns_.end())
                    {
                        // 没有则注册
                        provider = std::make_shared<Provider>(conn, addr);
                        conns_.insert_or_assign(conn, provider);
                    }
                    else
                    {
                        provider = it->second;
                    }
                    // 2.查找方法是否已经有其他提供者提供
                    std::set<Provider::ptr> &providers = providers_[method];
                    providers.insert(provider);
                }
                provider->appendMethod(method);
            }
            // 删除服务提供者，从 conns_ 和 providers_ 中移除。
            void removeProvider(const BaseConnection::ptr &conn)
            {
                Provider::ptr provider;
                std::set<std::string> methods;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = conns_.find(conn);
                    if (it == conns_.end())
                    {
                        LOG_ERROR("没有对应的服务提供者");
                        return;
                    }
                    provider = it->second;
                    methods = provider->methods_;
                    for (auto &name : methods)
                    {
                        auto &providers = providers_[name];
                        providers.erase(provider);
                    }
                    conns_.erase(conn);
                }
            }
            // 返回对应的服务提供者
            Provider::ptr getProvider(const BaseConnection::ptr &conn)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = conns_.find(conn);
                if (it == conns_.end())
                {
                    return Provider::ptr();
                }
                return it->second;
            }
            // 返回某方法的所有服务提供者的主机名
            std::vector<Address> methodHosts(const BaseConnection::ptr &conn, const std::string &method)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto &providers = providers_[method];
                std::vector<Address> hosts;
                for (auto &provider : providers)
                {
                    hosts.push_back(provider->host_);
                }
                return hosts;
            }

        private:
            std::mutex mutex_;
            std::unordered_map<std::string, std::set<Provider::ptr>> providers_; // 映射方法名到提供者集合
            std::unordered_map<BaseConnection::ptr, Provider::ptr> conns_;       // 映射连接到提供者
        };

        class DiscoverManager
        {
        public:
            using ptr = std::shared_ptr<DiscoverManager>;
            struct Discoverer
            {
                using ptr = std::shared_ptr<Discoverer>;
                std::mutex mutex_;
                BaseConnection::ptr conn_;
                std::set<std::string> methods;
                Discoverer(const BaseConnection::ptr &conn)
                    : conn_(conn)
                {
                }
                void appendMethod(const std::string &name)
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    methods.insert(name);
                }
            };

            Discoverer::ptr appendDiscover(const BaseConnection::ptr &conn, const std::string &method)
            {
                Discoverer::ptr discoverer;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = conns_.find(conn);
                    if (it == conns_.end())
                    {
                        discoverer = std::make_shared<Discoverer>(conn);
                    }
                    else
                    {
                        discoverer = it->second;
                    }
                    auto &discoverers = discoverers_[method];
                    discoverers.insert(discoverer);
                }
                discoverer->appendMethod(method);
                return discoverer;
            }
            void removeDiscover(const BaseConnection::ptr &conn)
            {
                Discoverer::ptr discoverer;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = conns_.find(conn);
                    if (it == conns_.end())
                    {
                        LOG_ERROR("没有对应的服务发现者");
                        return;
                    }

                    discoverer = it->second;
                    auto &methods = discoverer->methods;
                    for (auto &name : methods)
                    {
                        discoverers_[name].erase(discoverer);
                    }
                    conns_.erase(it);
                }
            }
            void onlineNotify(const std::string &method, const Address &host)
            {
                return notify(method, host, ServiceOptype::SERVICE_ONLINE);
            }
            void offlineNotify(const std::string &method, const Address &host)
            {
                return notify(method, host, ServiceOptype::SERVICE_OFFLINE);
            }

        private:
            void notify(const std::string &method, const Address &host, ServiceOptype optype)
            {
                std::unique_lock<std::mutex> lock(mutex_);

                // 1. 找到该方法需要通知的所有发现者
                auto it = discoverers_.find(method);
                if (it == discoverers_.end())
                {
                    return;
                }
                auto &discoverers = it->second;
                // 2. 对应发现者进行通知

                auto msg = MessageFactory::create<ServiceRequest>();
                msg->setId(generate_uuid_v4());
                msg->setMType(MType::REQ_SERVICE);
                msg->setMethod(method);
                msg->setHost(host);
                msg->setServiceOptype(optype);
                for (auto &discoverer : discoverers)
                {
                    auto &conn = discoverer->conn_;
                    conn->send(msg);
                }
            }

        private:
            std::mutex mutex_;
            std::unordered_map<std::string, std::set<Discoverer::ptr>> discoverers_;
            std::unordered_map<BaseConnection::ptr, Discoverer::ptr> conns_;
        };
        // 协调 ProviderManager 和 DiscovererManager，处理服务注册、发现和连接断开事件。
        class PDManager
        {
        public:
            using ptr = std::shared_ptr<PDManager>;
            PDManager()
                : providers_(std::make_shared<ProviderManager>()), discoverers_(std::make_shared<DiscoverManager>())
            {
            }
            // 处理 ServiceRequest，包括服务注册和发现。
            void onServiceRequest(const BaseConnection::ptr &conn, const ServiceRequest::ptr &msg)
            {
                auto optype = msg->ServiceOptype();
                if (optype == ServiceOptype::SERVICE_REGISTRY)
                {
                    auto method_name = msg->method();
                    auto address = msg->host();
                    LOG_INFO("{}{}注册服务{}", address.first, address.second, method_name);
                    providers_->appendProvider(conn, address, method_name);
                    // 上线通知
                    discoverers_->onlineNotify(method_name, address);
                    return registryResponse(conn, msg);
                }
                else if (optype == ServiceOptype::SERVICE_DISCOVERY)
                {
                    auto method_name = msg->method();
                    LOG_INFO("客户端进行{}服务发现", method_name);
                    discoverers_->appendDiscover(conn, method_name);
                    return discoveryResponse(conn, msg);
                }
                else
                {
                    LOG_ERROR("收到服务操作请求，但是操作类型错误");
                    return errorResponse(conn, msg);
                }
            }
            // 处理连接断开，清理提供者和发现者数据，通知下线。
            void onConnShutdown(const BaseConnection::ptr &conn)
            {
                // 1.获取服务提供者
                auto provider = providers_->getProvider(conn);
                if (provider.get() != nullptr)
                {
                    LOG_INFO("{}{}服务下线", provider->host_.first, provider->host_.second);
                    for (auto &method : provider->methods_)
                    {
                        discoverers_->offlineNotify(method, provider->host_);
                    }
                    providers_->removeProvider(conn);
                }
                discoverers_->removeDiscover(conn);
            }

        private:
            void errorResponse(const BaseConnection::ptr &conn, const ServiceRequest::ptr &msg)
            {
                auto msg_rsp = MessageFactory::create<ServiceResponse>();
                msg_rsp->setId(msg->Id());
                msg_rsp->setMType(MType::RSP_SERVICE);
                msg_rsp->setRcode(RCode::RCODE_INVALID_OPTYPE);
                msg_rsp->setServiceOptype(ServiceOptype::SERVICE_UNKNOW);
                conn->send(msg_rsp);
            }
            void registryResponse(const BaseConnection::ptr &conn, const ServiceRequest::ptr &msg)
            {
                auto msg_rsp = MessageFactory::create<ServiceResponse>();
                msg_rsp->setId(msg->Id());
                msg_rsp->setMType(MType::RSP_SERVICE);
                msg_rsp->setRcode(RCode::RCODE_OK);
                msg_rsp->setServiceOptype(ServiceOptype::SERVICE_REGISTRY);
                conn->send(msg_rsp);
            }
            // 响应提供者主机列表，检查是否为空。
            void discoveryResponse(const BaseConnection::ptr &conn, const ServiceRequest::ptr &msg)
            {
                auto msg_rsp = MessageFactory::create<ServiceResponse>();
                msg_rsp->setId(msg->Id());
                msg_rsp->setMType(MType::RSP_SERVICE);
                msg_rsp->setServiceOptype(ServiceOptype::SERVICE_DISCOVERY);
                std::vector<Address> hosts = providers_->methodHosts(conn, msg->method());
                if (hosts.empty())
                {
                    msg_rsp->setRcode(RCode::RCODE_NOT_FOUND_SERVICE);
                    return conn->send(msg_rsp);
                }
                msg_rsp->setRcode(RCode::RCODE_OK);
                msg_rsp->setMethod(msg->method());
                msg_rsp->setHosts(hosts);
                conn->send(msg_rsp);
            }

        private:
            ProviderManager::ptr providers_;
            DiscoverManager::ptr discoverers_;
        };
    };
};