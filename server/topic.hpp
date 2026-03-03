#pragma once
/**
 * 实现 Json-RPC 服务器端的主题（Topic）管理功能
 * 支持主题的创建、删除、订阅、取消订阅和消息发布。
 */
#include <unordered_set>
#include "../common/net.hpp"
#include "../common/message.hpp"

namespace Lyuih
{
    namespace server
    {
        class Subscriber
        {
        public:
            using ptr = std::shared_ptr<Subscriber>;
            Subscriber(const BaseConnection::ptr &conn)
                : conn_(conn)
            {
            }
            // 订阅主题时调用
            void appendTopic(const std::string &name)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                topics_.insert(name);
            }
            // 取消订阅或者主题被删除时调用
            void removeTopic(const std::string &name)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                topics_.erase(name);
            }
            bool find(const std::string &name)
            {
                return topics_.find(name) != topics_.end();
            }

        public:
            std::mutex mutex_;
            BaseConnection::ptr conn_;
            std::unordered_set<std::string> topics_;
        };
        class Topic
        {
        public:
            using ptr = std::shared_ptr<Topic>;
            Topic(std::string &name)
                : name_(name)
            {
            }
            // 新增订阅时候调用
            void appendSubscriber(const Subscriber::ptr &subscriber)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                subscribers_.insert(subscriber);
            }
            // 取消订阅或者订阅者连接断开时候调用
            void removeSubscriber(const Subscriber::ptr &subscriber)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                subscribers_.erase(subscriber);
            }
            // 收到消息发布请求的时候调用
            void pushMessage(const BaseMessage::ptr &msg)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                for (auto &subscriber : subscribers_)
                {
                    subscriber->conn_->send(msg);
                }
            }

        public:
            std::mutex mutex_;
            std::string name_;                                // 主题名
            std::unordered_set<Subscriber::ptr> subscribers_; // 当前主题的订阅者
        };
        class TopicManager
        {
        public:
            using ptr = std::shared_ptr<TopicManager>;
            void onTopicRequest(const BaseConnection::ptr &conn, const TopicRequest::ptr &topic_req)
            {
                // 1.获取topic类型
                auto otype = topic_req->TopicOptype();
                // 2.选择对应的处理方法
                bool ret = true;
                switch (otype)
                {
                    // 主题取消订阅
                case TopicOptype::TOPIC_CANCEL:
                    topicCancel(conn, topic_req);
                    break;
                    // 主题创建
                case TopicOptype::TOPIC_CREATE:
                    topicCreate(conn, topic_req);
                    break;
                    // 主题消息发布
                case TopicOptype::TOPIC_PUBLISH:
                    ret = topicPublish(conn, topic_req);
                    break;
                    // 主题删除
                case TopicOptype::TOPIC_REMOVE:
                    topicRemove(conn, topic_req);
                    break;
                    // 主题订阅
                case TopicOptype::TOPIC_SUBSCRIBE:
                    ret = topicSubscribe(conn, topic_req);
                    break;
                default:
                    return errResponse(conn, topic_req, RCode::RCODE_INVALID_OPTYPE);
                    break;
                }
                if (!ret)
                {
                    return errResponse(conn, topic_req, RCode::RCODE_NOT_FOUND_TOPIC);
                }
                return topicResponse(conn, topic_req);
            }

            // 一个订阅者在连接断开时的处理---删除其关联的数据
            void onShutdown(const BaseConnection::ptr &conn)
            {
                // 消息发布者断开连接，不需要任何操作；  消息订阅者断开连接需要删除管理数据
                std::vector<Topic::ptr> topics;
                Subscriber::ptr subscriber;
                {
                    // 1. 判断断开连接的是否是订阅者，不是的话则直接返回
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = subscribers_.find(conn);
                    if (it == subscribers_.end())
                    {
                        // 该订阅者已经关闭连接
                        LOG_ERROR("该订阅者已经关闭连接");
                        return;
                    }
                    subscriber = it->second;
                    // 2. 获取到订阅者退出，受影响的主题对象
                    for (auto &topic_name : subscriber->topics_)
                    {
                        auto topic_it = topics_.find(topic_name);
                        if (topic_it == topics_.end())
                        {
                            continue;
                        }
                        topics.push_back(topic_it->second);
                    }
                    // 3. 从订阅者映射信息中，删除订阅者

                    subscribers_.erase(it);
                }
                // 4. 从受影响的主题对象中，移除订阅者
                for (auto &topic : topics)
                {
                    topic->removeSubscriber(subscriber);
                }
            }

        private:
            // 出现错误
            void errResponse(const BaseConnection::ptr &conn, const TopicRequest::ptr &topic_req, RCode rcode)
            {
                auto msg = MessageFactory::create<TopicResponse>();
                msg->setId(topic_req->Id());
                msg->setMType(MType::RSP_TOPIC);
                msg->setRcode(rcode);
                conn->send(msg);
            }
            // 正常回复
            void topicResponse(const BaseConnection::ptr &conn, const TopicRequest::ptr &topic_req)
            {
                auto msg = MessageFactory::create<TopicResponse>();
                msg->setId(topic_req->Id());
                msg->setMType(MType::RSP_TOPIC);
                msg->setRcode(RCode::RCODE_OK);
                conn->send(msg);
            }

            void topicCancel(const BaseConnection::ptr &conn, const TopicRequest::ptr &topic_req)
            {
                // 类似与onShutdown,但是这里只是取消一个主题的订阅
                Topic::ptr topic;
                Subscriber::ptr subscriber;
                {
                    std::unique_lock<std::mutex> lock(mutex_);

                    auto it = subscribers_.find(conn);
                    if (it == subscribers_.end())
                    {
                        LOG_ERROR("该订阅者已经取消订阅");
                        return;
                    }
                    subscriber = it->second;
                    // 找到该需要取消的主题
                    std::string topic_name = topic_req->topic();
                    auto topic_it = topics_.find(topic_name);
                    if (topic_it == topics_.end())
                    {
                        LOG_ERROR("没有该主题");
                        return;
                    }
                }
                subscriber->removeTopic(topic_req->topic());
                topic->removeSubscriber(subscriber);
            }
            void topicCreate(const BaseConnection::ptr &conn, const TopicRequest::ptr &topic_req)
            {
                std::string topic_name = topic_req->topic();
                auto topic = std::make_shared<Topic>(topic_name);
                topic->name_ = topic_name;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    topics_.insert_or_assign(topic_name, topic);
                }
            }
            bool topicPublish(const BaseConnection::ptr &conn, const TopicRequest::ptr &topic_req)
            {
                // 1.找到主题对应的订阅者进行推送
                Topic::ptr topic;
                std::string topic_name = topic_req->topic();
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = topics_.find(topic_name);
                    if (it == topics_.end())
                    {
                        LOG_ERROR("没有对应的{}订阅主题", topic_name);
                        return false;
                    }
                    topic = it->second;
                }
                topic->pushMessage(topic_req);
                return true;
            }
            void topicRemove(const BaseConnection::ptr &conn, const TopicRequest::ptr &topic_req)
            {
                // 1. 查看当前主题，有哪些订阅者，然后从订阅者中将主题信息删除掉
                std::unordered_set<Subscriber::ptr> subscribers;
                std::string topic_name = topic_req->topic();
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = topics_.find(topic_name);
                    if (it == topics_.end())
                    {
                        LOG_ERROR("没有该主题,无需移除");
                        return;
                    }
                    // topics_.erase(it);
                    // 从订阅者中将主题信息删除掉
                    subscribers = it->second->subscribers_;
                    topics_.erase(it);
                }
                // 2. 删除主题的数据 -- 主题名称与主题对象的映射关系
                for (auto &subscriber : subscribers)
                {
                    subscriber->removeTopic(topic_name);
                }
            }
            bool topicSubscribe(const BaseConnection::ptr &conn, const TopicRequest::ptr &topic_req)
            {
                // 1. 先找出主题对象，以及订阅者对象
                //    如果没有找到主题--就要报错；  但是如果没有找到订阅者对象，那就要构造一个订阅者
                Topic::ptr topic;
                Subscriber::ptr subscriber;
                std::string topic_name = topic_req->topic();
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = topics_.find(topic_name);
                    if (it == topics_.end())
                    {
                        LOG_ERROR("没有对应的{}订阅主题", topic_name);
                        return false;
                    }
                    topic = it->second;
                    auto sub_it = subscribers_.find(conn);
                    if (sub_it == subscribers_.end())
                    {
                        // 要构造一个订阅者
                        subscriber = std::make_shared<Subscriber>(conn);
                    }
                    subscriber = sub_it->second;
                }
                // 2. 在主题对象中，新增一个订阅者对象关联的连接；  在订阅者对象中新增一个订阅的主题
                topic->appendSubscriber(subscriber);
                subscriber->appendTopic(topic_name);
                return true;
            }

        private:
            std::mutex mutex_;
            std::unordered_map<std::string, Topic::ptr> topics_;                   // 用于存储和查找所有已创建的主题，保持主题的快速检索和管理。
            std::unordered_map<BaseConnection::ptr, Subscriber::ptr> subscribers_; // 用于管理当前所有活跃的订阅者，根据连接对象查找订阅者，便于消息分发和订阅者状态维护。
        };

    };
};