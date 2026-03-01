#pragma once
#include <string>
#include <unordered_map>

namespace Lyuih
{
#define KEY_METHOD "method"
#define KEY_PARAMS "parameters"
#define KEY_TOPIC_KEY "topic_key"
#define KEY_TOPIC_MSG "topic_msg"
#define KEY_OPTYPE "optype"
#define KEY_HOST "host"
#define KEY_HOST_IP "ip"
#define KEY_HOST_PORT "port"
#define KEY_RCODE "rcode"
#define KEY_RESULT "result"
    enum class MType
    {
        REQ_RPC = 0, // RPC调用，如调用add方法
        RSP_RPC,
        REQ_TOPIC, // 发布订阅操作，如创建主题或者发布消息
        RSP_TOPIC,
        REQ_SERVICE, // 用于服务注册、发现或者上下线通知
        RSP_SERVICE
    };
    enum class RCode
    {
        RCODE_OK = 0,
        RCODE_PARSE_FAILED,
        RCODE_ERROR_MSGTYPE,
        RCODE_INVALID_MSG,
        RCODE_DISCONNECTED,
        RCODE_INVALID_PARAMS,
        RCODE_NOT_FOUND_SERVICE,
        RCODE_INVALID_OPTYPE,
        RCODE_NOT_FOUND_TOPIC,
        RCODE_INTERNAL_ERROR
    };
    static std::string errReason(RCode code)
    {
        static std::unordered_map<RCode, std::string> err_map = {
            {RCode::RCODE_OK, "成功处理！"},
            {RCode::RCODE_PARSE_FAILED, "消息解析失败！"},
            {RCode::RCODE_ERROR_MSGTYPE, "消息类型错误！"},
            {RCode::RCODE_INVALID_MSG, "无效消息"},
            {RCode::RCODE_DISCONNECTED, "连接已断开！"},
            {RCode::RCODE_INVALID_PARAMS, "无效的Rpc参数！"},
            {RCode::RCODE_NOT_FOUND_SERVICE, "没有找到对应的服务！"},
            {RCode::RCODE_INVALID_OPTYPE, "无效的操作类型"},
            {RCode::RCODE_NOT_FOUND_TOPIC, "没有找到对应的主题！"},
            {RCode::RCODE_INTERNAL_ERROR, "内部错误！"}};
        auto it = err_map.find(code);
        if (it == err_map.end())
        {
            return "未知错误！";
        }
        return it->second;
    }
    enum class RType
    {
        REQ_ASYNC = 0, // 使用std::future异步获取结果
        REQ_CALLBACK   // 通过回调函数处理响应
    };
    enum class TopicOptype
    {
        TOPIC_CREATE = 0, // 创建主题
        TOPIC_REMOVE,
        TOPIC_SUBSCRIBE,
        TOPIC_CANCEL,
        TOPIC_PUBLISH // 向主题发布消息
    };
    enum class ServiceOptype
    {
        SERVICE_REGISTRY = 0, // 服务提供者注册服务
        SERVICE_DISCOVERY,    // 客户端查询可用服务
        SERVICE_ONLINE,       // 通知服务上线
        SERVICE_OFFLINE,      // 通知服务下线
        SERVICE_UNKNOW        // 处理未知操作
    };
}