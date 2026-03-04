#pragma once
/**
 * 用于实现 Json-RPC 客户端的请求发送和响应处理机制。
 */
#include <future>

#include "../common/net.hpp"
#include "../common/uuidUtil.hpp"

namespace Lyuih
{
    namespace client
    {
        // 负责管理客户端的请求发送和响应处理
        class Requestor
        {
        public:
            using ptr = std::shared_ptr<Requestor>;
            using RequestCallback = std::function<void(const BaseMessage::ptr &)>;
            using AsyncResponse = std::future<BaseMessage::ptr>;
            // 定义请求描述，存储请求的元消息，用于追踪和处理响应
            struct ResquestDescribe
            {
                using ptr = std::shared_ptr<ResquestDescribe>;
                ResquestDescribe(const BaseMessage::ptr &req, RType rtype)
                    : request_(req), rtype_(rtype)
                {
                }
                BaseMessage::ptr request_;                // 存储发送的请求
                RType rtype_;                             // 请求类型
                std::promise<BaseMessage::ptr> response_; // 异步响应对象，用于获取请求对应的结果消息
                RequestCallback callback_;                // 请求回调函数，处理请求完成后的回调操作
            };
            // 处理服务器返回的响应，根据rid查找请求描述，调用回调或设置异步结果。
            void onResponse(const BaseConnection::ptr &conn, const BaseMessage::ptr &msg)
            {
                std::string id = msg->Id();
                ResquestDescribe::ptr rdb = getDescribe(id);
                if (rdb.get() == nullptr)
                {
                    LOG_ERROR("{}没有对应的请求描述", id);
                    return;
                }
                // 异步
                if (rdb->rtype_ == RType::REQ_ASYNC)
                {
                    rdb->response_.set_value(msg);
                }
                // 回调
                else if (rdb->rtype_ == RType::REQ_CALLBACK)
                {
                    if (rdb->callback_)
                    {
                        rdb->callback_(msg);
                    }
                }
                else
                {
                    LOG_ERROR("请求类型未知");
                }
                removeDescribe(id);
            }
            // 发送异步请求，返回future获取响应
            bool send(const BaseConnection::ptr &conn, const BaseMessage::ptr &req, AsyncResponse &async_rsp)
            {
                ResquestDescribe::ptr rdb = appendDescribe(req, RType::REQ_ASYNC);
                if (rdb.get() == nullptr)
                {
                    LOG_ERROR("构建请求描述失败");
                    return false;
                }
                conn->send(req);
                async_rsp = rdb->response_.get_future();
                return true;
            }
            // 发送同步请求，阻塞等待响应。
            bool send(const BaseConnection::ptr &conn, const BaseMessage::ptr &req, BaseMessage::ptr &rsp)
            {
                AsyncResponse rsp_future;
                bool ret = send(conn, req, rsp_future);
                if (ret == false)
                {
                    return false;
                }
                rsp = rsp_future.get();

                return true;
            }
            // 发送请求并指定回调函数处理响应。
            bool send(const BaseConnection::ptr &conn, const BaseMessage::ptr &req, const RequestCallback &cb)
            {
                ResquestDescribe::ptr rdb = appendDescribe(req, RType::REQ_CALLBACK, cb);
                if (rdb.get() == nullptr)
                {
                    LOG_ERROR("构建请求描述失败");
                    return false;
                }
                conn->send(req);
                return true;
            }

        private:
            ResquestDescribe::ptr appendDescribe(const BaseMessage::ptr &req, RType r_type, const RequestCallback &cb = RequestCallback())
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ResquestDescribe::ptr rdb = std::make_shared<ResquestDescribe>(req, r_type);
                if (RType::REQ_CALLBACK == r_type && cb)
                {
                    rdb->callback_ = cb;
                }
                request_desc_.insert_or_assign(req->Id(), rdb);
                return rdb;
            }

            ResquestDescribe::ptr getDescribe(const std::string &id)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = request_desc_.find(id);
                if (it == request_desc_.end())
                {
                    return ResquestDescribe::ptr();
                }
                return it->second;
            }
            void removeDescribe(const std::string &id)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                request_desc_.erase(id);
            }

        private:
            std::mutex mutex_;
            std::unordered_map<std::string, ResquestDescribe::ptr> request_desc_;
        };
    };

};