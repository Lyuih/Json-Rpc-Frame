#pragma once
/**
 * 消息分发
 * 根据消息类型自动调用对应的消息处理回调函数，实现解耦的消息处理流程。
 */
#include <memory>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "abstract.hpp"
#include "fields.hpp"
#include "log.hpp"

namespace Lyuih
{
    // 定义消息处理回调的接口，声明纯虚函数onMessage
    class CallBack
    {
    public:
        using ptr = std::shared_ptr<CallBack>;
        virtual void onMesssage(const BaseConnection::ptr &conn, const BaseMessage::ptr &msg) = 0;
    };

    // 通过模板参数 T，确保回调处理特定消息类型（如 RpcRequest），避免手动类型转换的错误。
    template <typename T>
    class CallBackT : public CallBack
    {
    public:
        using ptr = std::shared_ptr<CallBackT<T>>;
        using CallBackMessage = std::function<void(const BaseConnection::ptr &, const std::shared_ptr<T> &)>;
        CallBackT(const CallBackMessage &callback)
            : callback_(callback)
        {
        }
        virtual void onMesssage(const BaseConnection::ptr &conn, const BaseMessage::ptr &msg) override
        {
            // 转换为更具体的子类类型
            auto type_msg = std::dynamic_pointer_cast<T>(msg);
            if (callback_)
            {
                callback_(conn, type_msg);
            }
        }

    private:
        CallBackMessage callback_;
    };

    // 根据消息类型自动调用对应的消息处理回调函数，实现解耦的消息处理流程。
    class Dispatcher
    {
    public:
        using ptr = std::shared_ptr<Dispatcher>;
        template <typename T>
        void registerHandler(MType m_type, const typename CallBackT<T>::CallBackMessage &callback)
        {
            // 回调函数注册
            std::unique_lock<std::mutex> lock(mutex_);
            auto cb = std::make_shared<CallBackT<T>>(callback);
            callbacks_.insert_or_assign(m_type, cb);
        }
        virtual void onMesssage(const BaseConnection::ptr &conn, const BaseMessage::ptr &msg)
        {
            // 消息分发
            std::unique_lock<std::mutex> lock(mutex_);
            // 1.获取消息类型
            auto m_type = msg->MType();
            // 2.找到对应回调
            auto it = callbacks_.find(m_type);
            if (it == callbacks_.end())
            {
                LOG_ERROR("没有对应回调");
                conn->shutdown();
                return;
            }
            it->second->onMesssage(conn, msg);
        }

    private:
        std::mutex mutex_;
        std::unordered_map<MType, CallBack::ptr> callbacks_;
    };
};