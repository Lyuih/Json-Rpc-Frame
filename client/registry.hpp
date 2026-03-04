#pragma once
/**
 * 这是一段用C++实现的PRC系统的客户端部分，包括3个类
 * Provider
 * MethodHost
 * Discoverer
 * 分别负责服务注册、主机管理和服务发现
 */

#include "requestor.hpp"
#include <unordered_set>

namespace Lyuih
{
    namespace client
    {
        // 负责将服务方法注册到特定主机
        class Provider
        {
        public:
            using ptr = std::shared_ptr<Provider>;
            Provider(const Requestor::ptr &requestor)
                : requestor_(requestor)
            {
            }
            // 通过指定的连接将服务方法注册到指定主机
            bool registryMethod(const BaseConnection::ptr &conn, const std::string &method, const Address &host)
            {
                auto msg_srq = MessageFactory::create<ServiceRequest>();
                msg_srq->setId(generate_uuid_v4());
                msg_srq->setHost(host);
                msg_srq->setMethod(method);
                msg_srq->setMType(MType::REQ_SERVICE);
                msg_srq->setServiceOptype(ServiceOptype::SERVICE_REGISTRY);
                BaseMessage::ptr msg_srp;
                bool ret = requestor_->send(conn, msg_srq, msg_srp);
                if (ret == false)
                {
                    LOG_ERROR("{}服务注册失败", method);
                    return false;
                }
                auto service_msg_rsp = std::dynamic_pointer_cast<ServiceResponse>(msg_srp);
                if (service_msg_rsp.get() == nullptr)
                {
                    LOG_ERROR("服务注册向下转型失败");
                    return false;
                }
                if (service_msg_rsp->rcode() != RCode::RCODE_OK)
                {
                    LOG_ERROR("服务注册请求错误:{}", errReason(service_msg_rsp->rcode()));
                    return false;
                }
                return true;
            }

        private:
            Requestor::ptr requestor_;
        };
        // 类管理提供某服务方法的主机列表，并支持主机的添加、删除和选择。
        class MethodHost
        {
        public:
            using ptr = std::shared_ptr<MethodHost>;
            MethodHost()
                : index_(0)
            {
            }
            MethodHost(const std::vector<Address> &hosts)
                : index_(0), hosts_(hosts)
            {
            }
            void appendHost(const Address &host)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                hosts_.push_back(host);
            }
            void removeHost(const Address &host)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = hosts_.begin();
                for (; it != hosts_.end(); ++it)
                {
                    if ((*it) == host)
                    {
                        break;
                    }
                }
                hosts_.erase(it);
            }
            // 轮询
            Address chooseHost()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                size_t pos = index_++ % hosts_.size();
                return hosts_[pos];
            }
            bool empty()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return hosts_.empty();
            }

        private:
            std::mutex mutex_;
            size_t index_;
            std::vector<Address> hosts_;
        };
        // 负责服务发现，管理服务方法与主机列表的映射，并处理服务的上线和下线请求。
        class Discoverer
        {
        public:
            using ptr = std::shared_ptr<Discoverer>;
            // 定义回调函数类型，用于处理服务下线事件。
            using OfflineCallback = std::function<void(const Address &)>;
            Discoverer(const Requestor::ptr &requestor, const OfflineCallback &cb)
                : offline_callback_(cb), requestor_(requestor)
            {
            }
            // 发现指定方法的服务提供主机地址。
            bool serviceDiscovery(const BaseConnection::ptr &conn, const std::string &method, Address &host)
            {
                // 1.如果本地有该服务的主机地址,直接使用
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = method_hosts_.find(method);
                    if (it != method_hosts_.end())
                    {
                        if (it->second->empty() == false)
                        {
                            host = it->second->chooseHost();
                            return true;
                        }
                    }
                }
                // 2.当前服务的提供者为空,使用 _requestor->send 发送请求并等待响应。
                auto msg_srq = MessageFactory::create<ServiceRequest>();
                msg_srq->setId(generate_uuid_v4());
                msg_srq->setHost(host);
                msg_srq->setMethod(method);
                msg_srq->setMType(MType::REQ_SERVICE);
                msg_srq->setServiceOptype(ServiceOptype::SERVICE_DISCOVERY);

                BaseMessage::ptr msg_srp;
                bool ret = requestor_->send(conn, msg_srq, msg_srp);
                if (ret == false)
                {
                    LOG_ERROR("{}服务发现失败", method);
                    return false;
                }
                auto service_msg_rsp = std::dynamic_pointer_cast<ServiceResponse>(msg_srp);
                if (service_msg_rsp.get() == nullptr)
                {
                    LOG_ERROR("服务发现向下转型失败");
                    return false;
                }
                if (service_msg_rsp->rcode() != RCode::RCODE_OK)
                {
                    LOG_ERROR("服务发现请求错误:{}", errReason(service_msg_rsp->rcode()));
                    return false;
                }
                // 3.获取最新的服务主机列表
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto method_hosts = std::make_shared<MethodHost>(service_msg_rsp->hosts());
                    if (method_hosts->empty())
                    {
                        LOG_ERROR("{}服务发现失败,没有能提供服务的主机", method);
                        return false;
                    }
                    host = method_hosts->chooseHost();
                    method_hosts_[method] = method_hosts;
                }
                return true;
            }
            // 提供给Dispatcher模块进行服务上线下线请求处理的回调函数
            void onServiceRequest(const BaseConnection::ptr &conn, const ServiceRequest::ptr &msg)
            {
                // 1.判断是上线还是下线
                auto optype = msg->ServiceOptype();
                std::string method = msg->method();
                std::unique_lock<std::mutex> lock(mutex_);
                if (optype == ServiceOptype::SERVICE_ONLINE)
                {
                    // 上线请求
                    auto it = method_hosts_.find(method);
                    if (it == method_hosts_.end())
                    {
                        auto method_host = std::make_shared<MethodHost>();
                        method_host->appendHost(msg->host());
                        method_hosts_[method] = method_host;
                    }
                    else
                    {
                        it->second->appendHost(msg->host());
                    }
                }
                else if (optype == ServiceOptype::SERVICE_OFFLINE)
                {
                    // 下线请求
                    auto it = method_hosts_.find(method);
                    if (it == method_hosts_.end())
                    {
                        return;
                    }
                    else
                    {
                        it->second->removeHost(msg->host());
                        offline_callback_(msg->host());
                    }
                }
            }

        private:
            OfflineCallback offline_callback_;
            std::mutex mutex_;
            // 方法名到 MethodHost 共享指针的映射，存储服务方法与主机列表的关联。
            std::unordered_map<std::string, MethodHost::ptr> method_hosts_;
            // 用于发送消息的 Requestor 对象。
            Requestor::ptr requestor_;
        };
    };
};