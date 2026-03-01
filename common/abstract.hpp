#pragma once
/**
 * 项目抽象层
 */
#include <string>
#include <functional>
#include <memory>
#include "fields.hpp"

// 对缓冲区的抽象，用于管理网络通信中的字节流。
class BaseBuffer
{
public:
    using ptr = std::shared_ptr<BaseBuffer>;
    virtual size_t readableBytes() = 0;                   // 返回缓冲区可读字节数
    virtual int32_t peekInt32() = 0;                      // 查看缓冲区头部4字节，但不移除。
    virtual void retrieveInt32() = 0;                     // 移除头部4字节。
    virtual int readInt32() = 0;                          // 读取并移除头部4字节。
    virtual std::string retrieveAsString(size_t len) = 0; // 读取特定长度字符串并移除。
};

// 对所有所有消息的抽象，包括RPC请求/响应、主题操作、服务支持。
class BaseMessage
{
public:
    using ptr = std::shared_ptr<BaseMessage>;
    virtual void setId(const std::string &id) { id_ = id; };              // 设置消息id表示消息唯一性
    virtual std::string Id() { return id_; }                              // 读取消息id
    virtual void setMType(const Lyuih::MType &mType) { mType_ = mType; }; // 设置消息类型
    virtual Lyuih::MType MType() { return mType_; };                      // 读取消息类型
    virtual std::string serialize() = 0;                                  // 序列化
    virtual bool unserialize(const std::string &msg) = 0;                 // 反序列化
    virtual bool check() = 0;                                             // 验证消息有效性

private:
    Lyuih::MType mType_; // 定义消息类别
    std::string id_;     // 消息id
};

// 对网络连接的抽象，表示客户端或者服务端单条连接。
class BaseConnection
{
public:
    using ptr = std::shared_ptr<BaseConnection>;
    virtual bool send(const BaseMessage::ptr &msg) = 0; // 发送BaseMessage消息
    virtual void shutdown() = 0;                        // 关闭连接
    virtual bool connected() = 0;                       // 检测连接状态
};

// 抽象协议解析，负责消息的序列化和检测。
class BaseProtocol
{
public:
    using ptr = std::shared_ptr<BaseProtocol>;
    virtual bool onMessage(BaseBuffer::ptr &buf, BaseMessage::ptr &msg) = 0; // 从缓冲区解析出BaseMessage对象
    virtual std::string serialize(const BaseMessage::ptr &msg) = 0;          // 将BaseMessage序列化为字符串（包含Length和Value）
    virtual bool canProcessed(const BaseBuffer::ptr &buf) = 0;               // 基于LV格式的length字段来检测缓冲区是否包含完整消息
};

using ConnectionCallback = std::function<void(const BaseConnection::ptr &)>;
using CloseCallback = std::function<void(const BaseConnection::ptr &)>;
using MessageCallback = std::function<void(const BaseConnection::ptr &, BaseMessage::ptr &)>;

// 抽象服务端，负责服务端管理连接和消息处理
class BaseServer
{
public:
    using ptr = std::shared_ptr<BaseServer>;
    virtual void setConnectionCallback(const ConnectionCallback &conn_cb) { conn_callback_ = conn_cb; } // 建立连接时的回调函数
    virtual void setCloseback(const CloseCallback &close_cb) { close_callback_ = close_cb; }            // 关闭连接时的回调
    virtual void setMessageCallback(const MessageCallback &msg_cb) { msg_callback_ = msg_cb; }          // 收到消息时的回调
    virtual void start() = 0;                                                                           // 启动服务端
protected:
    ConnectionCallback conn_callback_;
    CloseCallback close_callback_;
    MessageCallback msg_callback_;
};

class BaseClient
{
public:
    using ptr = std::shared_ptr<BaseClient>;
    virtual void setConnectionCallback(const ConnectionCallback &conn_cb) { conn_callback_ = conn_cb; } // 建立连接时的回调函数
    virtual void setCloseback(const CloseCallback &close_cb) { close_callback_ = close_cb; }            // 关闭连接时的回调
    virtual void setMessageCallback(const MessageCallback &msg_cb) { msg_callback_ = msg_cb; }          // 收到消息时的回调
    virtual bool connect() = 0;                                                                         // 建立与服务端的连接
    virtual bool send(const BaseMessage::ptr & msg) = 0;                                                    // 发送消息
    virtual void shutdown() = 0;                                                                        // 关闭连接
    virtual bool connected() = 0;                                                                       // 检测连接状态
    virtual BaseConnection::ptr connection() = 0;                                                       // 获取当前连接对象
protected:
    ConnectionCallback conn_callback_;
    CloseCallback close_callback_;
    MessageCallback msg_callback_;
};