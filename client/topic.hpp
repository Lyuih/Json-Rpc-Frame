#pragma once
/**
 * 实现 Json-RPC 客户端的主题（Topic）管理功能，支持创建、删除、订阅、取消订阅和发布主题消息。
 */

#include "requestor.hpp"
#include "../common/message.hpp"
#include <unordered_set>

namespace Lyuih
{
    namespace client
    {
        class TopicManager
        {
        public:
            using ptr = std::shared_ptr<TopicManager>;
            using SubCallback = std::function<void(const std::string &topic_key, const std::string &msg)>;
            TopicManager(const Requestor::ptr &requestor)
                : requestor_(requestor)
            {
            }
            bool create(const BaseConnection::ptr &conn, const std::string &topic_key)
            {
                return commonRequest(conn, topic_key, TopicOptype::TOPIC_CREATE);
            }
            bool remove(const BaseConnection::ptr &conn, const std::string &topic_key)
            {
                return commonRequest(conn, topic_key, TopicOptype::TOPIC_REMOVE);
            }
            bool subscribe(const BaseConnection::ptr &conn, const std::string &topic_key, const SubCallback &cb)
            {
                appendSubscribe(topic_key, cb);
                bool ret = commonRequest(conn, topic_key, TopicOptype::TOPIC_SUBSCRIBE);
                if (ret == false)
                {
                    removeSubscribe(topic_key);
                    return false;
                }
                return true;
            }
            bool cancel(const BaseConnection::ptr &conn, const std::string &topic_key)
            {
                removeSubscribe(topic_key);
                return commonRequest(conn, topic_key, TopicOptype::TOPIC_CANCEL);
            }
            bool publish(const BaseConnection::ptr &conn, const std::string &topic_key, const std::string &msg)
            {
                return commonRequest(conn, topic_key, TopicOptype::TOPIC_PUBLISH,msg);
            }
            // 处理并分发收到的主题消息
            void onPublish(const BaseConnection::ptr &conn, const TopicRequest::ptr &msg)
            {
                // 1.检查是否为pushlish类型
                auto type = msg->TopicOptype();
                if (type != TopicOptype::TOPIC_PUBLISH)
                {
                    LOG_ERROR("收到错误类型主题操作");
                    return;
                }
                std::string topic_key = msg->topic();
                std::string topic_msg = msg->TopicMsg();
                auto cb = getSubscribe(topic_key);
                if (!cb)
                {
                    LOG_ERROR("收到了{}主题消息,但是该消息无主题回调", topic_key);
                    return;
                }
                return cb(topic_key, topic_msg);
            }

        private:
            void appendSubscribe(const std::string &topic_key, const SubCallback &cb)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                topic_callbacks_.insert_or_assign(topic_key, cb);
            }
            void removeSubscribe(const std::string &topic_key)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                topic_callbacks_.erase(topic_key);
            }
            const SubCallback getSubscribe(const std::string &topic_key)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto it = topic_callbacks_.find(topic_key);
                if (it == topic_callbacks_.end())
                {
                    return SubCallback();
                }
                return it->second;
            }
            // 通用方法，发送主题操作请求并等待响应。
            bool commonRequest(const BaseConnection::ptr &conn, const std::string &topic_key, TopicOptype type, const std::string &msg = "")
            {
                auto msg_req = MessageFactory::create<TopicRequest>();
                msg_req->setId(generate_uuid_v4());
                msg_req->setMType(MType::REQ_TOPIC);
                msg_req->setTopicOptype(type);
                msg_req->setTopic(topic_key);
                if (type == TopicOptype::TOPIC_PUBLISH)
                {
                    msg_req->setTopicMsg(msg);
                }
                BaseMessage::ptr msg_rsp;
                bool ret = requestor_->send(conn, msg_req, msg_rsp);
                if (ret == false)
                {
                    LOG_ERROR("主题请求失败");
                    return false;
                }
                auto topic_msg_rsp = std::dynamic_pointer_cast<TopicResponse>(msg_rsp);
                if (topic_msg_rsp.get() == nullptr)

                {
                    LOG_ERROR("主题向下转型失败");
                    return false;
                }
                if (topic_msg_rsp->rcode() != RCode::RCODE_OK)
                {
                    LOG_ERROR("rpc请求错误:{}", errReason(topic_msg_rsp->rcode()));
                    return false;
                }
                return true;
            }

        private:
            std::mutex mutex_;
            std::unordered_map<std::string, SubCallback> topic_callbacks_; // 映射主题到回调函数
            Requestor::ptr requestor_;                                     // 底层请求管理对象
        };
    };
};
