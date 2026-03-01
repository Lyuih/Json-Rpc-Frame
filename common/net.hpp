#pragma once
/**
 * 具象层通信部分实现
 */
#include <memory>
#include <mutex>
#include <thread>
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/CountDownLatch.h>
#include <muduo/net/EventLoopThread.h>
#include <muduo/net/TcpClient.h>
#include "abstract.hpp"
#include "log.hpp"
#include "message.hpp"

// MuduoBuffer继承BaseBuffer,提供缓冲区操作
namespace Lyuih
{
    class MuduoBuffer : public BaseBuffer
    {
    public:
        using ptr = std::shared_ptr<MuduoBuffer>;
        MuduoBuffer(muduo::net::Buffer *buf)
            : buf_(buf)
        {
        }
        // 返回缓冲区可读字节数
        virtual size_t readableBytes() override
        {
            return buf_->readableBytes();
        }
        // 查看缓冲区头部4字节，但不移除。
        virtual int32_t peekInt32() override
        {
            return buf_->peekInt32();
        }
        // 移除头部4字节。
        virtual void retrieveInt32() override
        {
            return buf_->retrieveInt32();
        }
        // 读取并移除头部4字节。
        virtual int readInt32() override
        {
            return buf_->readInt32();
        }
        // 读取特定长度字符串并移除。
        virtual std::string retrieveAsString(size_t len) override
        {
            return buf_->retrieveAsString(len);
        }

    private:
        muduo::net::Buffer *buf_; 
    };

    // MuduoBuffer工厂模式
    class BufferFactory
    {
    public:
        template <typename... Args>
        static BaseBuffer::ptr create(Args &&...args)
        {
            return std::make_shared<MuduoBuffer>(std::forward<Args>(args)...);
        }
    };

    // LVProtocol 继承 BaseProtocol
    // 协议格式：
    //|--Length--|--MType--|--IdLen--|--Id--|--Body--|
    // Length：消息总长度，不包括Length  4字节
    // MType: 消息类型 4字节
    // IdLen: 消息ID长度 4字节
    // Body: 消息正文（Json格式） 大小不定
    class LVProtocol : public BaseProtocol
    {
    public:
        using ptr = std::shared_ptr<LVProtocol>;
        // 从缓冲区解析出BaseMessage对象
        virtual bool onMessage(BaseBuffer::ptr &buf, BaseMessage::ptr &msg) override
        {
            if (!canProcessed(buf))
                return false;
            // 1.依次解析出length,MType,Idlen
            int32_t len = buf->readInt32();
            Lyuih::MType m_type = (Lyuih::MType)buf->readInt32();
            int32_t id_len = buf->readInt32();
            std::string id = buf->retrieveAsString(id_len);
            // 2.计算出body长度并提取
            int32_t body_len = len - mtypeFieldsLength - idlenFieldsLength - id_len;
            std::string body = buf->retrieveAsString(body_len);
            // 3.反序列化到msg
            // a.根据类型创建message对象
            msg = MessageFactory::create(m_type);
            if (msg == BaseMessage::ptr())
            {
                LOG_ERROR("消息类型错误,创建BaseMessage失败");
                return false;
            }
            // b.反序列化
            bool ret = msg->unserialize(body);
            if (ret == false)
            {
                LOG_ERROR("缓冲区解析出BaseMessage对象,反序列化失败");
                return false;
            }
            // 设置消息类型
            msg->setMType(m_type);
            // 设置消息id
            msg->setId(id);
            return true;
        }
        // 将BaseMessage序列化为字符串（包含Length和Value）
        virtual std::string serialize(const BaseMessage::ptr &msg) override
        {
            // 1. 将msg序列化获取消息组主体字符串
            std::string body = msg->serialize();
            // 2.提取必要信息,MType,id
            int32_t m_type = ::htonl((int32_t)msg->MType());
            std::string id = msg->Id();
            // 3. 计算必要信息:length,idlen,并转化为网络字节序
            int32_t id_len = ::htonl(id.size());
            int32_t len = mtypeFieldsLength + idlenFieldsLength + id.size() + body.size();
            int32_t len_h = ::htonl(len);
            // 4.填充进字符串
            std::string ret;
            ret.reserve(len + lenFieldsLength); // 扩容
            ret.append((char *)&len_h, lenFieldsLength);
            ret.append((char *)&m_type, mtypeFieldsLength);
            ret.append((char *)&id_len, idlenFieldsLength);
            ret.append(id);
            ret.append(body);
            return ret;
        }
        // 基于LV格式的length字段来检测缓冲区是否包含完整消息
        virtual bool canProcessed(const BaseBuffer::ptr &buf) override
        {
            // 1.先判断消息长度是否大于lenFieldsLength
            if (buf->readableBytes() < lenFieldsLength)
            {
                LOG_WARN("消息头消息缺失");
                return false;
            }
            // 2. 再判断消息是否完整
            int32_t len = buf->peekInt32();
            if (buf->readableBytes() < len + lenFieldsLength)
            {
                LOG_WARN("消息不完整");
                return false;
            }
            return true;
        }

    private:
        // 定义各个字段的长度 常量
        const size_t lenFieldsLength = 4;
        const size_t mtypeFieldsLength = 4;
        const size_t idlenFieldsLength = 4;
    };

    class ProtocolFactory
    {
    public:
        template <typename... Args>
        static BaseProtocol::ptr create(Args &&...args)
        {
            return std::make_shared<LVProtocol>(std::forward<Args>(args)...);
        }
    };

    // MuduoConnection 继承 BaseConnection
    // 管理Tcp连接和消息发送
    // 属性：muduo::net::TcpConnectPrt 和 BaseProtocol
    // 通过协议对象序列化消息，确保格式一致
    class MuduoConnection : public BaseConnection
    {
    public:
        using ptr = std::shared_ptr<MuduoConnection>;
        MuduoConnection(const muduo::net::TcpConnectionPtr &conn, const BaseProtocol::ptr &protocol)
            : conn_(conn), protocol_(protocol)
        {
        }
        // 发送BaseMessage消息
        virtual void send(const BaseMessage::ptr &msg) override
        {
            // 通过_protocol序列化消息，使用conn发送消息
            std::string str = protocol_->serialize(msg);
            conn_->send(str);
        }
        // 关闭连接
        virtual void shutdown() override
        {
            conn_->shutdown();
        }
        // 检测连接状态
        virtual bool connected() override
        {
            return conn_->connected();
        }

    private:
        muduo::net::TcpConnectionPtr conn_;
        BaseProtocol::ptr protocol_;
    };

    class ConnectionFactory
    {
    public:
        template <typename... Args>
        static BaseConnection::ptr create(Args &&...args)
        {
            return std::make_shared<MuduoConnection>(std::forward<Args>(args)...);
        }
    };

    // MuduoServer 继承 BaseServer
    // 基于muduo::net::TcpServer 管理TCP服务器
    class MuduoServer : public BaseServer
    {
    public:
        using ptr = std::shared_ptr<MuduoServer>;
        MuduoServer(int port)
            : server_(&baseLoop_, muduo::net::InetAddress("0.0.0.0", port), "MuduoServer", muduo::net::TcpServer::kReusePort), protocol_(ProtocolFactory::create())
        {
        }

        // 启动服务端
        virtual void start() override
        {
            server_.setConnectionCallback(std::bind(&MuduoServer::onConnection, this, std::placeholders::_1));
            server_.setMessageCallback(std::bind(&MuduoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            // 开始监听
            server_.start();
            // 开始循环事件监控
            baseLoop_.loop();
        }

    private:
        // 监控客户端的连接
        void onConnection(const muduo::net::TcpConnectionPtr &connectionPtr)
        {
            // 两种情况,连接时回调,断开时回调
            if (connectionPtr->connected())
            {
                // 连接时
                LOG_INFO("连接建立");

                // 1.建立与本地连接管理的映射关系
                auto conn = ConnectionFactory::create(connectionPtr, protocol_);
                {
                    // 建立映射
                    std::unique_lock<std::mutex> lock(mutex_);

                    conns_.insert(std::make_pair(connectionPtr, conn));
                }
                // 2.调用回调函数
                if (conn_callback_)
                {
                    conn_callback_(conn);
                }
            }
            else
            {
                // 断开时
                LOG_INFO("连接断开");
                // 1.删除映射
                BaseConnection::ptr conn;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = conns_.find(connectionPtr);
                    if (it == conns_.end())
                    {
                        LOG_WARN("映射不存在");
                        return;
                    }
                    conn = it->second;
                    conns_.erase(it);
                }
                // 2.调用回调函数
                if (close_callback_)
                {
                    close_callback_(conn);
                }
            }
        }
        // 处理客户端发送的消息，解析并交给用户回调
        void onMessage(const muduo::net::TcpConnectionPtr &connectionPtr, muduo::net::Buffer *buffer, muduo::Timestamp t)
        {
            LOG_INFO("服务端有消息到达");
            auto muduo_buffer = BufferFactory::create(buffer);
            for (;;)
            {
                if (protocol_->canProcessed(muduo_buffer) == false)
                {
                    // 防止恶意注入大量数据
                    if (muduo_buffer->readableBytes() > maxDataSize)
                    {
                        LOG_ERROR("数据过载");
                        connectionPtr->shutdown();
                        return;
                    }
                    LOG_WARN("数据不完整");
                    // 继续等待更多数据
                    break;
                }
                BaseMessage::ptr msg;
                bool ret = protocol_->onMessage(muduo_buffer, msg);
                if (ret == false)
                {
                    LOG_ERROR("解析数据失败,关闭连接");
                    connectionPtr->shutdown();
                    return;
                }
                // 从conns获取对应的BaseConnection::ptr 调用msg_callback_处理消息
                BaseConnection::ptr conn;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    auto it = conns_.find(connectionPtr);
                    if (it == conns_.end())
                    {
                        LOG_WARN("未找到对应映射");
                        connectionPtr->shutdown();
                        return;
                    }
                    conn = it->second;
                }

                LOG_INFO("调用回调函数进行消息处理");
                if (msg_callback_)
                {
                    msg_callback_(conn, msg);
                }
            }
        }

    private:
        const size_t maxDataSize = (1 << 16);                                         // 防止缓冲区溢出
        muduo::net::EventLoop baseLoop_;                                              // 事件循环，负责监控和处理所有的I/O事件
        muduo::net::TcpServer server_;                                                // Tcp服务器
        std::mutex mutex_;                                                            // 线程安全
        std::unordered_map<muduo::net::TcpConnectionPtr, BaseConnection::ptr> conns_; // 管理线程
        BaseProtocol::ptr protocol_;
    };
    class ServerFactory
    {
    public:
        template <typename... Args>
        static BaseServer::ptr create(Args &&...args)
        {
            return std::make_shared<MuduoServer>(std::forward<Args>(args)...);
        }
    };

    class MuduoClient : public BaseClient
    {
    public:
        using ptr = std::shared_ptr<MuduoClient>;
        MuduoClient(const std::string &ip, int port)
            : baseloop_(loopthread_.startLoop()), downlatch_(1), client_(baseloop_, muduo::net::InetAddress(ip, port), "MuduoClient"), protocol_(ProtocolFactory::create())
        {
            ;
        }
        // 建立与服务端的连接
        virtual void connect() override
        {
            LOG_INFO("建立回调关联");
            client_.setConnectionCallback(std::bind(&MuduoClient::onConnection, this, std::placeholders::_1));
            client_.setMessageCallback(std::bind(&MuduoClient::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
            client_.connect();
            downlatch_.wait();
            LOG_INFO("连接服务器成功");
        }
        // 发送消息
        virtual bool send(const BaseMessage::ptr &msg) override
        {
            if (conn_->connected() == false)
            {
                LOG_INFO("连接断开");
                return false;
            }
            else
            {
                conn_->send(msg);
                return true;
            }
        }
        // 关闭连接
        virtual void shutdown() override
        {
            client_.disconnect();
        }
        // 检测连接状态
        virtual bool connected() override
        {
            if (conn_)
            {
                return conn_->connected();
            }
        }
        // 获取当前连接对象
        virtual BaseConnection::ptr connection() override
        {
            return conn_;
        }

    private:
        void onConnection(const muduo::net::TcpConnectionPtr &connectionPtr)
        {
            // 1.判断是建立连接还是断开连接
            if (connectionPtr->connected())
            {
                LOG_INFO("客户端建立连接");
                conn_ = ConnectionFactory::create(connectionPtr, protocol_);
                downlatch_.countDown(); // 先建立连接再 countDown
            }
            else
            {
                LOG_INFO("客户端断开连接");
                conn_.reset();
            }
        }
        void onMessage(const muduo::net::TcpConnectionPtr &connectionPtr, muduo::net::Buffer *buffer, muduo::Timestamp t)
        {
            LOG_INFO("客户端有消息到达");
            auto muduo_buffer = BufferFactory::create(buffer);
            for (;;)
            {
                if (protocol_->canProcessed(muduo_buffer) == false)
                {
                    // 防止恶意注入大量数据
                    if (muduo_buffer->readableBytes() > maxDataSize)
                    {
                        LOG_ERROR("数据过载");
                        connectionPtr->shutdown();
                        return;
                    }
                    LOG_WARN("数据不完整");
                    // 继续等待更多数据
                    break;
                }
                BaseMessage::ptr msg;
                bool ret = protocol_->onMessage(muduo_buffer, msg);
                if (ret == false)
                {
                    LOG_ERROR("解析数据失败,关闭连接");
                    connectionPtr->shutdown();
                    return;
                }

                LOG_INFO("调用回调函数进行消息处理");
                if (msg_callback_)
                {
                    msg_callback_(conn_, msg);
                }
            }
        }

    private:
        const size_t maxDataSize = (1 << 16);    // 最大数据缓冲区大小，防止缓冲区溢出
        muduo::net::EventLoopThread loopthread_; // 事件循环线程，负责管理I/O事件线程

        BaseConnection::ptr conn_;        // 连接对象指针，管理与服务器的连接
        muduo::net::EventLoop *baseloop_; // 事件循环指针，处理所有的I/O事件
        muduo::CountDownLatch downlatch_; // 倒计时锁，用于连接同步，等待连接建立
        muduo::net::TcpClient client_;    // TCP客户端对象，管理与服务器的TCP连接
        BaseProtocol::ptr protocol_;      // 协议对象指针，用于消息的序列化和反序列化
    };

    class ClientFactory
    {
    public:
        template <typename... Args>
        static BaseClient::ptr create(Args &&...args)
        {
            return std::make_shared<MuduoClient>(std::forward<Args>(args)...);
        }
    };

}
