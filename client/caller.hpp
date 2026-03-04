#pragma once
/**
 * rpc请求接口
 */

#include "requestor.hpp"

namespace Lyuih
{
    namespace client
    {
        // Json-RPC客户端的高层封装，基于Requestor提供：发送RPC请求、处理响应、简化接口等功能

        class RpcCaller
        {
        public:
            using ptr = std::shared_ptr<RpcCaller>;
            using JsonAsyncResponse = std::future<Json::Value>;                    // 用于异步请求返回结果
            using JsonResponseCallback = std::function<void(const Json::Value &)>; // 回调函数类型，接收Json::Value结果
            RpcCaller(const Requestor::ptr &requestor)
                : requestor_(requestor)
            {
            }
            // 同步调用RPC请求方法，阻塞等待响应，提取Json::Value结果
            bool call(const BaseConnection::ptr &conn, const std::string &method, const Json::Value &params, Json::Value &result)
            {
                LOG_INFO("开始同步rpc调用{}", method);
                auto req_msg = MessageFactory::create<RpcRequest>();
                req_msg->setId(generate_uuid_v4());
                req_msg->setMethod(method);
                req_msg->setMType(MType::REQ_RPC);
                req_msg->setParam(params);
                BaseMessage::ptr rsp_msg;
                bool ret = requestor_->send(conn, std::dynamic_pointer_cast<BaseMessage>(req_msg), rsp_msg);
                if (ret == false)
                {
                    LOG_ERROR("同步请求失败");
                    return false;
                }
                LOG_INFO("收到消息,进行解析获取结果");
                auto rpc_rsp_msg = std::dynamic_pointer_cast<RpcResponse>(rsp_msg);
                if (rpc_rsp_msg.get() == nullptr)
                {
                    LOG_ERROR("向下转型失败");
                    return false;
                }
                if (rpc_rsp_msg->rcode() != RCode::RCODE_OK)
                {
                    LOG_ERROR("rpc请求错误:{}", errReason(rpc_rsp_msg->rcode()));
                    return false;
                }
                result = rpc_rsp_msg->result();
                LOG_DEBUG("结果设置完毕");
                return true;
            }
            // 异步调用 RPC 方法，返回 std::future<Json::Value> 用于获取结果。
            bool call(const BaseConnection::ptr &conn, const std::string &method, const Json::Value &params, JsonAsyncResponse &result)
            {
                LOG_INFO("开始异步rpc调用{}", method);
                auto req_msg = MessageFactory::create<RpcRequest>();
                req_msg->setId(generate_uuid_v4());
                req_msg->setMethod(method);
                req_msg->setMType(MType::REQ_RPC);
                req_msg->setParam(params);

                auto json_promise = std::make_shared<std::promise<Json::Value>>();
                result = json_promise->get_future();

                Requestor::RequestCallback cb = std::bind(&RpcCaller::CallBackAsync, this, json_promise, std::placeholders::_1);

                bool ret = requestor_->send(conn, std::dynamic_pointer_cast<BaseMessage>(req_msg), cb);
                if (ret == false)
                {
                    LOG_ERROR("同步请求失败");
                    return false;
                }
                return true;
            }
            // 通过回调方式调用 RPC 方法，响应时调用用户提供的回调
            bool call(const BaseConnection::ptr &conn, const std::string &method, const Json::Value params, const JsonResponseCallback &cb)
            {
                LOG_INFO("开始异步rpc调用{}", method);
                auto req_msg = MessageFactory::create<RpcRequest>();
                req_msg->setId(generate_uuid_v4());
                req_msg->setMethod(method);
                req_msg->setMType(MType::REQ_RPC);
                req_msg->setParam(params);

                Requestor::RequestCallback req_cb = std::bind(&RpcCaller::CallBack, this, cb, std::placeholders::_1);

                bool ret = requestor_->send(conn, std::dynamic_pointer_cast<BaseMessage>(req_msg), req_cb);
                if (ret == false)
                {
                    LOG_ERROR("同步请求失败");
                    return false;
                }
                return true;
            }

        private:
            // 处理回调模式的响应，提取 Json::Value 并调用用户回调。
            void CallBack(const JsonResponseCallback &cb, const BaseMessage::ptr &msg)
            {
                auto rpc_rsp_msg = std::dynamic_pointer_cast<RpcResponse>(msg);
                if (rpc_rsp_msg.get() == nullptr)
                {
                    LOG_ERROR("向下转型失败");
                    return;
                }
                if (rpc_rsp_msg->rcode() != RCode::RCODE_OK)
                {
                    LOG_ERROR("rpc请求错误:{}", errReason(rpc_rsp_msg->rcode()));
                    return;
                }
                cb(rpc_rsp_msg->result());
            }
            // 处理异步模式的响应，设置 std::promise 的值。
            void CallBackAsync(std::shared_ptr<std::promise<Json::Value>> reslut, const BaseMessage::ptr &msg)
            {
                auto rpc_rsp_msg = std::dynamic_pointer_cast<RpcResponse>(msg);
                if (rpc_rsp_msg.get() == nullptr)
                {
                    LOG_ERROR("向下转型失败");
                    return;
                }
                if (rpc_rsp_msg->rcode() != RCode::RCODE_OK)
                {
                    LOG_ERROR("rpc请求错误:{}", errReason(rpc_rsp_msg->rcode()));
                    return;
                }
                reslut->set_value(rpc_rsp_msg->result());
            }

            Requestor::ptr requestor_; // 底层请求管理对象
        };
    };

};
