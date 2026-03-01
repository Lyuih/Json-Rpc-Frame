#pragma once
/**
 * 消息抽象实现
 */

#include "fields.hpp"
#include "abstract.hpp"
#include "JsonUtil.hpp"
#include "uuidUtil.hpp"
#include "log.hpp"

namespace Lyuih
{
    // 先让JsonMessage继承BaseMessage,覆写序列化和反序列化
    class JsonMessage : public BaseMessage
    {
    public:
        using ptr = std::shared_ptr<JsonMessage>;
        virtual std::string serialize() override
        {
            // 序列化
            std::string msg;
            bool ret = JsonUtil::serialize(body_, msg);
            if (ret == false)
            {
                return std::string();
            }
            return msg;
        }
        virtual bool unserialize(const std::string &msg) override
        {
            return JsonUtil::unserialize(msg, body_);
        }

    protected:
        Json::Value body_; // Json数据
    };

    // JsonRequest 继承 JsonMessage 作为请求消息的基类,无额外实现
    class JsonRequest : public JsonMessage
    {
    public:
        using ptr = std::shared_ptr<JsonRequest>;
    };

    // JsonResponse 继承 JsonMessage 作为响应消息的基类,检查消息rcode字段
    class JsonResponse : public JsonMessage
    {
    public:
        using ptr = std::shared_ptr<JsonResponse>;
        virtual bool check() override
        {
            if (body_[KEY_RCODE].isNull() == true)
            {
                LOG_ERROR("响应中没有响应状态码!");
                return false;
            }
            else if (body_[KEY_RCODE].isIntegral() == false)
            {
                LOG_ERROR("响应状态码类型错误");
                return false;
            }
            return true;
        }
        // 设置/获取响应状态码
        virtual Lyuih::RCode rcode() { return (Lyuih::RCode)body_[KEY_RCODE].asInt(); }
        virtual void setRcode(Lyuih::RCode rcode) { body_[KEY_RCODE] = (int)rcode; }
    };

    // RpcRequest继承JsonRequest,表示RPC请求消息,包含方法名和参数
    // 用于客户端发送RPC请求,服务器解析后路由到提供服务的服务端
    class RpcRequest : public JsonRequest
    {
    public:
        using ptr = std::shared_ptr<RpcRequest>;
        virtual bool check() override
        {
            // RPC的请求格式为{"method":"xxx","parameters":{...}}
            if (body_[KEY_METHOD].isNull() == true)
            {
                LOG_ERROR("请求中没有方法");
                return false;
            }
            else if (body_[KEY_METHOD].isString() == false)
            {
                LOG_ERROR("方法类型错误");
                return false;
            }
            if (body_[KEY_PARAMS].isNull() == true)
            {
                LOG_ERROR("请求中没有参数");
                return false;
            }
            else if (body_[KEY_PARAMS].isObject() == false)
            {
                LOG_ERROR("参数类型错误");
                return false;
            }
            return true;
        }
        // 设置/获取 方法/参数
        virtual std::string method() { return body_[KEY_METHOD].asString(); }
        virtual void setMethod(const std::string &method) { body_[KEY_METHOD] = method; }
        virtual Json::Value param() { return body_[KEY_PARAMS]; }
        virtual void setParam(const Json::Value &param) { body_[KEY_PARAMS] = param; }
    };

    // TopicRequest 继承 JsonRequest，发布订阅请求消息，支持主题操作（创建、订阅、发布）
    // 用于客户端发送主题请求，服务端由TopicManager处理
    class TopicRequest : public JsonRequest
    {
    public:
        using ptr = std::shared_ptr<TopicRequest>;
        virtual bool check() override
        {
            // Topic的格式 {"key_topic":"xxx",otype" : x,"topic_msg":"xxx"}
            if (body_[KEY_TOPIC_KEY].isNull())
            {
                LOG_ERROR("请求中没有主题");
                return false;
            }
            else if (body_[KEY_TOPIC_KEY].isString() == false)
            {
                LOG_ERROR("主题类型错误");
                return false;
            }

            if (body_[KEY_OPTYPE].isNull() || body_[KEY_OPTYPE].isIntegral() == false)
            {
                LOG_ERROR("请求中没有主题类型或者主题类型错误");
                return false;
            }
            if (body_[KEY_TOPIC_MSG].isNull() || body_[KEY_TOPIC_MSG].isString() == false)
            {
                LOG_ERROR("请求中没有主题消息或者类型错误");
                return false;
            }
            return true;
        }
        virtual std::string topic() { return body_[KEY_TOPIC_KEY].asString(); }
        virtual void setTopic(const std::string &topic) { body_[KEY_TOPIC_KEY] = topic; }
        virtual Lyuih::TopicOptype TopicOptype() { return (Lyuih::TopicOptype)body_[KEY_OPTYPE].asInt(); }
        virtual void setTopicOptype(const Lyuih::TopicOptype &topic_opytpe) { body_[KEY_OPTYPE] = (int)topic_opytpe; }
        virtual std::string TopicMsg() { return body_[KEY_TOPIC_MSG].asString(); }
        virtual void setTopicMsg(const std::string &msg) { body_[KEY_TOPIC_MSG] = msg; }
    };

    // ServiceRequest 继承 JsonRequest，表示服务注册与发现请求消息，支持服务注册、发现和上下线
    // 用于服务提供者或者客户端发现服务，由RegistryManager处理
    using Address = std::pair<std::string, int16_t>;
    class ServiceRequest : public JsonRequest
    {
    public:
        using ptr = std::shared_ptr<ServiceRequest>;
        virtual bool check() override
        {
            // Service的格式 {"method":"xxx","optype":x,"host":{"ip":"xxx","port":xxx}}
            if (body_[KEY_METHOD].isNull() == true)
            {
                LOG_ERROR("服务请求中没有方法名");
                return false;
            }
            else if (body_[KEY_METHOD].isString() == false)
            {
                LOG_ERROR("服务请求中方法类型错误");
                return false;
            }
            if (body_[KEY_OPTYPE].isNull() || body_[KEY_OPTYPE].isIntegral() == false)
            {
                LOG_ERROR("服务请求中没有方法或者方法类型错误");
                return false;
            }
            if (body_[KEY_OPTYPE] != (int)Lyuih::ServiceOptype::SERVICE_DISCOVERY && (body_[KEY_HOST].isNull() == true ||
                                                                                      body_[KEY_HOST].isObject() == false ||
                                                                                      body_[KEY_HOST][KEY_HOST_IP].isNull() == true ||
                                                                                      body_[KEY_HOST][KEY_HOST_IP].isString() == false ||
                                                                                      body_[KEY_HOST][KEY_HOST_PORT].isNull() == true ||
                                                                                      body_[KEY_HOST][KEY_HOST_PORT].isIntegral() == false))
            {
                LOG_ERROR("服务请求中主机地址信息错误");
                return false;
            }
            return true;
        }
        virtual std::string method() { return body_[KEY_METHOD].asString(); }
        virtual void setMethod(const std::string &method) { body_[KEY_METHOD] = method; }
        virtual Lyuih::ServiceOptype ServiceOptype() { return (Lyuih::ServiceOptype)body_[KEY_OPTYPE].asInt(); }
        virtual void setServiceOptype(const Lyuih::ServiceOptype &service_opytpe) { body_[KEY_OPTYPE] = (int)service_opytpe; }
        virtual Address host()
        {
            Json::Value address = body_[KEY_HOST];
            std::string ip = address[KEY_HOST_IP].asString();
            int16_t port = address[KEY_HOST_PORT].asInt();
            return {ip, port};
        }
        virtual void setHost(const Address &address)
        {
            std::string ip = address.first;
            int port = address.second;
            Json::Value addr;
            addr[KEY_HOST_IP] = ip;
            addr[KEY_HOST_PORT] = port;
            body_[KEY_HOST] = addr;
        }
    };

    // RpcResponse 继承 JsonResponse 表示RPC响应消息，包含状态码和调用结果
    // RPC的响应格式{"rcode":"xx","result":}
    // result允许任意JSON类型，灵活支持不同类型返回值
    class RpcResponse : public JsonResponse
    {
    public:
        using ptr = std::shared_ptr<RpcResponse>;
        virtual bool check() override
        {
            if (body_[KEY_RCODE].isNull() == true)
            {
                LOG_ERROR("Rpc响应中没有响应状态码!");
                return false;
            }
            else if (body_[KEY_RCODE].isIntegral() == false)
            {
                LOG_ERROR("Rpc响应状态码类型错误");
                return false;
            }
            if (body_[KEY_RESULT].isNull() || body_[KEY_RESULT].isObject() == false)
            {
                LOG_ERROR("Rpc响应中没有返回信息或者返回学校类型错误");
                return false;
            }
            return true;
        }
        virtual Json::Value result() { return body_[KEY_RESULT]; }
        virtual void setResult(const Json::Value &root) { body_[KEY_RESULT] = root; }
    };

    // TopicResponse 继承 JsonResponse,确认主题操作的状态
    class TopicResponse : public JsonResponse
    {
    public:
        using ptr = std::shared_ptr<TopicResponse>;
    };

    // ServiceResponse 继承 JsonResponse，表示服务注册与响应消息，包含
    // 状态码、操作类型和主机地址
    // 返回服务结果，客户端根据此连接服务提供者
    // 可用服务地址列表：{"rcode":"ok","optype":1,"method":"add",
    //                    "host":[{"ip":"","port":8080},{}]}
    class ServiceResponse : public JsonResponse
    {
    public:
        using ptr = std::shared_ptr<ServiceResponse>;
        virtual bool check() override
        {
            if (body_[KEY_RCODE].isNull() == true)
            {
                LOG_ERROR("Rpc响应中没有响应状态码!");
                return false;
            }
            else if (body_[KEY_RCODE].isIntegral() == false)
            {
                LOG_ERROR("Rpc响应状态码类型错误");
                return false;
            }
            if (body_[KEY_METHOD].isNull() == true)
            {
                LOG_ERROR("服务响应中没有方法名");
                return false;
            }
            else if (body_[KEY_METHOD].isString() == false)
            {
                LOG_ERROR("服务响应中方法类型错误");
                return false;
            }
            if (body_[KEY_OPTYPE].isNull() || body_[KEY_OPTYPE].isIntegral() == false)
            {
                LOG_ERROR("服务响应中没有方法或者方法类型错误");
                return false;
            }
            if (body_[KEY_OPTYPE].asInt() == int(Lyuih::ServiceOptype::SERVICE_DISCOVERY) && (body_[KEY_HOST].isNull() || body_[KEY_HOST].isArray() == false))
            {
                LOG_ERROR("服务响应中主机信息缺失或者类型错误");
                return false;
            }
            return true;
        }
        virtual std::string method() { return body_[KEY_METHOD].asString(); }
        virtual void setMethod(const std::string &method) { body_[KEY_METHOD] = method; }
        virtual Lyuih::ServiceOptype ServiceOptype() { return (Lyuih::ServiceOptype)body_[KEY_OPTYPE].asInt(); }
        virtual void setServiceOptype(const Lyuih::ServiceOptype &service_opytpe) { body_[KEY_OPTYPE] = (int)service_opytpe; }
        virtual std::vector<Address> hosts()
        {
            Json::Value arr = body_[KEY_HOST];
            const int sz = arr.size();
            std::vector<Address> v(sz);
            for (int i = 0; i < sz; ++i)
            {
                const std::string ip = arr[i][KEY_HOST_IP].asString();
                const int16_t port = arr[i][KEY_HOST_PORT].asInt();
                v[i] = Address{ip, port};
            }
            return v;
        }
        virtual void setHosts(const std::vector<Address> &hosts)
        {
            for (Address addr : hosts)
            {
                Json::Value root;
                root[KEY_HOST_IP] = addr.first;
                root[KEY_HOST_PORT] = addr.second;
                body_[KEY_HOST].append(root);
            }
        }
    };

    // 定义一个工厂类，根据消息类型（MYype）创建对应的消息对象
    // 调用者无需直接实例化具体消息类，降低代码耦合
    class MessageFactory
    {
    public:
        static BaseMessage::ptr create(MType m_type)
        {
            switch (m_type)
            {
            case MType::REQ_RPC:
                return std::make_shared<RpcRequest>();
            case MType::RSP_RPC:
                return std::make_shared<RpcResponse>();
            case MType::REQ_TOPIC:
                return std::make_shared<TopicRequest>();
            case MType::RSP_TOPIC:
                return std::make_shared<TopicResponse>();
            case MType::REQ_SERVICE:
                return std::make_shared<ServiceRequest>();
            case MType::RSP_SERVICE:
                return std::make_shared<ServiceResponse>();
            }
            return BaseMessage::ptr();
        }
        template <typename T, typename... Args>
        static std::shared_ptr<T> create(Args &&...args)
        {
            // 静态断言：检查T是否是BaseMessage的子类
            static_assert(std::is_base_of<BaseMessage, T>::value,
                          "T must be a subclass of BaseMessage");
            return std::make_shared<T>(std::forward<Args>(args)...);
        }
    };
}