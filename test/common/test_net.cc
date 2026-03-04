#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>
#include <cstring>
#include <arpa/inet.h>

#include "../../common/net.hpp"
#include "../../common/message.hpp"
#include "../../common/fields.hpp"
#include "../../common/log.hpp"

using namespace Lyuih;

// ==================== MockBuffer ====================
// 注意：peekInt32/readInt32 必须做 ntohl，与 muduo::Buffer 行为一致

class MockBuffer : public BaseBuffer
{
public:
    explicit MockBuffer(const std::string &data = "") : data_(data), pos_(0) {}

    void append(const std::string &data) { data_ += data; }

    size_t readableBytes() override { return data_.size() - pos_; }

    int32_t peekInt32() override
    {
        if (readableBytes() < 4)
            return 0;
        int32_t val;
        std::memcpy(&val, data_.data() + pos_, 4);
        return static_cast<int32_t>(::ntohl(static_cast<uint32_t>(val)));
    }

    void retrieveInt32() override { pos_ += 4; }

    int readInt32() override
    {
        int32_t val = peekInt32();
        retrieveInt32();
        return val;
    }

    std::string retrieveAsString(size_t len) override
    {
        if (len > readableBytes())
            len = readableBytes();
        std::string result(data_.data() + pos_, len);
        pos_ += len;
        return result;
    }

private:
    std::string data_;
    size_t pos_;
};

static BaseBuffer::ptr make_buf(const std::string &data = "")
{
    return std::make_shared<MockBuffer>(data);
}

// ==================== 辅助：构造各类消息 ====================

static BaseMessage::ptr make_rpc_req(const std::string &method = "add",
                                     const std::string &id = "req_001")
{
    auto msg = MessageFactory::create<RpcRequest>();
    auto req = std::static_pointer_cast<RpcRequest>(msg);
    Json::Value params;
    params["a"] = 1;
    req->setMethod(method);
    req->setParam(params);
    req->setId(id);
    req->setMType(MType::REQ_RPC);
    return msg;
}

static BaseMessage::ptr make_rpc_rsp(int answer = 42,
                                     const std::string &id = "rsp_001")
{
    auto msg = MessageFactory::create<RpcResponse>();
    auto rsp = std::static_pointer_cast<RpcResponse>(msg);
    Json::Value result;
    result["answer"] = answer;
    rsp->setRcode(RCode::RCODE_OK);
    rsp->setResult(result);
    rsp->setId(id);
    rsp->setMType(MType::RSP_RPC);
    return msg;
}

static BaseMessage::ptr make_topic_req(const std::string &topic = "news",
                                       const std::string &id = "tp_001")
{
    auto msg = MessageFactory::create<TopicRequest>();
    auto req = std::static_pointer_cast<TopicRequest>(msg);
    req->setTopic(topic);
    req->setTopicOptype(TopicOptype::TOPIC_PUBLISH);
    req->setTopicMsg("breaking");
    req->setId(id);
    req->setMType(MType::REQ_TOPIC);
    return msg;
}

static BaseMessage::ptr make_topic_rsp(const std::string &id = "tr_001")
{
    auto msg = MessageFactory::create<TopicResponse>();
    auto rsp = std::static_pointer_cast<TopicResponse>(msg);
    rsp->setRcode(RCode::RCODE_OK);
    rsp->setId(id);
    rsp->setMType(MType::RSP_TOPIC);
    return msg;
}

static BaseMessage::ptr make_service_req(const std::string &method = "calc",
                                         const std::string &id = "sr_001")
{
    auto msg = MessageFactory::create<ServiceRequest>();
    auto req = std::static_pointer_cast<ServiceRequest>(msg);
    req->setMethod(method);
    req->setServiceOptype(ServiceOptype::SERVICE_REGISTRY);
    req->setHost({"10.0.0.1", 7070});
    req->setId(id);
    req->setMType(MType::REQ_SERVICE);
    return msg;
}

static BaseMessage::ptr make_service_rsp(const std::string &id = "srsp_001")
{
    auto msg = MessageFactory::create<ServiceResponse>();
    auto rsp = std::static_pointer_cast<ServiceResponse>(msg);
    rsp->setRcode(RCode::RCODE_OK);
    rsp->setMethod("calc");
    rsp->setServiceOptype(ServiceOptype::SERVICE_DISCOVERY);
    rsp->setHosts({{"10.0.0.1", 7070}, {"10.0.0.2", 7071}});
    rsp->setId(id);
    rsp->setMType(MType::RSP_SERVICE);
    return msg;
}

// ==================== LVProtocol 单元测试 ====================

class LVProtocolTest : public ::testing::Test
{
protected:
    BaseProtocol::ptr proto = ProtocolFactory::create();
};

// -------------------- canProcessed --------------------

TEST_F(LVProtocolTest, CanProcessed_EmptyBuffer_False)
{
    auto buf = make_buf("");
    EXPECT_FALSE(proto->canProcessed(buf));
}

TEST_F(LVProtocolTest, CanProcessed_LessThan4Bytes_False)
{
    auto buf = make_buf("\x00\x00");
    EXPECT_FALSE(proto->canProcessed(buf));
}

TEST_F(LVProtocolTest, CanProcessed_OnlyLengthHeader_False)
{
    std::string full = proto->serialize(make_rpc_req());
    auto buf = make_buf(full.substr(0, 4));
    EXPECT_FALSE(proto->canProcessed(buf));
}

TEST_F(LVProtocolTest, CanProcessed_IncompleteBody_False)
{
    std::string full = proto->serialize(make_rpc_req());
    auto buf = make_buf(full.substr(0, full.size() - 1));
    EXPECT_FALSE(proto->canProcessed(buf));
}

TEST_F(LVProtocolTest, CanProcessed_ExactCompleteMessage_True)
{
    std::string full = proto->serialize(make_rpc_req());
    auto buf = make_buf(full);
    EXPECT_TRUE(proto->canProcessed(buf));
}

TEST_F(LVProtocolTest, CanProcessed_MoreThanOneMessage_True)
{
    std::string full = proto->serialize(make_rpc_req("add", "id_1")) + proto->serialize(make_rpc_req("sub", "id_2"));
    auto buf = make_buf(full);
    EXPECT_TRUE(proto->canProcessed(buf));
}

// -------------------- serialize / onMessage 往返 --------------------

TEST_F(LVProtocolTest, RoundTrip_RpcRequest)
{
    auto msg = make_rpc_req("mul", "id_rpc");
    std::string raw = proto->serialize(msg);
    EXPECT_FALSE(raw.empty());

    auto buf = make_buf(raw);
    ASSERT_TRUE(proto->canProcessed(buf));

    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));
    ASSERT_NE(out, nullptr);

    EXPECT_EQ(out->MType(), MType::REQ_RPC);
    EXPECT_EQ(out->Id(), "id_rpc");

    auto req = std::dynamic_pointer_cast<RpcRequest>(out);
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->method(), "mul");
    EXPECT_EQ(req->param()["a"].asInt(), 1);
    EXPECT_TRUE(req->check());
}

TEST_F(LVProtocolTest, RoundTrip_RpcResponse)
{
    auto msg = make_rpc_rsp(99, "id_rsp");
    auto buf = make_buf(proto->serialize(msg));

    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));

    EXPECT_EQ(out->MType(), MType::RSP_RPC);
    EXPECT_EQ(out->Id(), "id_rsp");

    auto rsp = std::dynamic_pointer_cast<RpcResponse>(out);
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(rsp->rcode(), RCode::RCODE_OK);
    EXPECT_EQ(rsp->result()["answer"].asInt(), 99);
}

TEST_F(LVProtocolTest, RoundTrip_TopicRequest)
{
    auto msg = make_topic_req("sports", "id_tp");
    auto buf = make_buf(proto->serialize(msg));

    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));

    EXPECT_EQ(out->MType(), MType::REQ_TOPIC);
    EXPECT_EQ(out->Id(), "id_tp");

    auto req = std::dynamic_pointer_cast<TopicRequest>(out);
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->topic(), "sports");
    EXPECT_EQ(req->TopicMsg(), "breaking");
    EXPECT_TRUE(req->check());
}

TEST_F(LVProtocolTest, RoundTrip_TopicResponse)
{
    auto msg = make_topic_rsp("id_tr");
    auto buf = make_buf(proto->serialize(msg));

    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));

    EXPECT_EQ(out->MType(), MType::RSP_TOPIC);
    EXPECT_EQ(out->Id(), "id_tr");

    auto rsp = std::dynamic_pointer_cast<TopicResponse>(out);
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(rsp->rcode(), RCode::RCODE_OK);
}

TEST_F(LVProtocolTest, RoundTrip_ServiceRequest)
{
    auto msg = make_service_req("calc", "id_sr");
    auto buf = make_buf(proto->serialize(msg));

    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));

    EXPECT_EQ(out->MType(), MType::REQ_SERVICE);
    EXPECT_EQ(out->Id(), "id_sr");

    auto req = std::dynamic_pointer_cast<ServiceRequest>(out);
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->method(), "calc");
    EXPECT_EQ(req->host().first, "10.0.0.1");
    EXPECT_EQ(req->host().second, 7070);
    EXPECT_TRUE(req->check());
}

TEST_F(LVProtocolTest, RoundTrip_ServiceResponse)
{
    auto msg = make_service_rsp("id_srsp");
    auto buf = make_buf(proto->serialize(msg));

    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));

    EXPECT_EQ(out->MType(), MType::RSP_SERVICE);
    EXPECT_EQ(out->Id(), "id_srsp");

    auto rsp = std::dynamic_pointer_cast<ServiceResponse>(out);
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(rsp->rcode(), RCode::RCODE_OK);
    EXPECT_EQ(rsp->hosts().size(), 2u);
    EXPECT_EQ(rsp->hosts()[0].first, "10.0.0.1");
    EXPECT_EQ(rsp->hosts()[1].second, 7071);
    EXPECT_TRUE(rsp->check());
}

// -------------------- 空 Id --------------------

TEST_F(LVProtocolTest, RoundTrip_EmptyId)
{
    auto msg = make_rpc_req("add", "");
    auto buf = make_buf(proto->serialize(msg));

    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));
    EXPECT_EQ(out->Id(), "");
    EXPECT_EQ(out->MType(), MType::REQ_RPC);
}

// -------------------- 粘包：两条消息在同一 buffer --------------------

TEST_F(LVProtocolTest, Sticky_TwoMessagesInOneBuffer)
{
    std::string raw = proto->serialize(make_rpc_req("add", "id_1")) + proto->serialize(make_rpc_req("sub", "id_2"));
    auto buf = make_buf(raw);

    BaseMessage::ptr out1, out2;

    ASSERT_TRUE(proto->canProcessed(buf));
    ASSERT_TRUE(proto->onMessage(buf, out1));
    EXPECT_EQ(out1->Id(), "id_1");
    EXPECT_EQ(std::dynamic_pointer_cast<RpcRequest>(out1)->method(), "add");

    ASSERT_TRUE(proto->canProcessed(buf));
    ASSERT_TRUE(proto->onMessage(buf, out2));
    EXPECT_EQ(out2->Id(), "id_2");
    EXPECT_EQ(std::dynamic_pointer_cast<RpcRequest>(out2)->method(), "sub");

    EXPECT_FALSE(proto->canProcessed(buf));
    EXPECT_EQ(buf->readableBytes(), 0u);
}

// -------------------- 粘包：三条不同类型消息 --------------------

TEST_F(LVProtocolTest, Sticky_ThreeDifferentTypesInOneBuffer)
{
    std::string raw = proto->serialize(make_rpc_req("f1", "m1")) + proto->serialize(make_topic_req("t1", "m2")) + proto->serialize(make_service_req("s1", "m3"));
    auto buf = make_buf(raw);

    BaseMessage::ptr out;

    ASSERT_TRUE(proto->onMessage(buf, out));
    EXPECT_EQ(out->MType(), MType::REQ_RPC);
    EXPECT_EQ(out->Id(), "m1");

    ASSERT_TRUE(proto->onMessage(buf, out));
    EXPECT_EQ(out->MType(), MType::REQ_TOPIC);
    EXPECT_EQ(out->Id(), "m2");

    ASSERT_TRUE(proto->onMessage(buf, out));
    EXPECT_EQ(out->MType(), MType::REQ_SERVICE);
    EXPECT_EQ(out->Id(), "m3");

    EXPECT_FALSE(proto->canProcessed(buf));
}

// -------------------- 非法 MType --------------------

TEST_F(LVProtocolTest, OnMessage_InvalidMType_ReturnsFalse)
{
    std::string raw = proto->serialize(make_rpc_req());
    // 篡改第 4~8 字节（MType 字段）为非法值 0xFF（网络字节序写入）
    int32_t bad = ::htonl(0xFF);
    std::memcpy(&raw[4], &bad, 4);

    auto buf = make_buf(raw);
    ASSERT_TRUE(proto->canProcessed(buf));

    BaseMessage::ptr out;
    EXPECT_FALSE(proto->onMessage(buf, out));
}

// -------------------- 数据不足时 onMessage 返回 false --------------------

TEST_F(LVProtocolTest, OnMessage_InsufficientData_ReturnsFalse)
{
    std::string full = proto->serialize(make_rpc_req());
    auto buf = make_buf(full.substr(0, full.size() - 1));

    BaseMessage::ptr out;
    EXPECT_FALSE(proto->onMessage(buf, out));
}

// -------------------- 序列化幂等性 --------------------

TEST_F(LVProtocolTest, Serialize_TwiceIsSame)
{
    auto msg = make_rpc_req("foo", "bar");
    EXPECT_EQ(proto->serialize(msg), proto->serialize(msg));
}

// -------------------- 序列化后 length 字段校验 --------------------

TEST_F(LVProtocolTest, Serialize_LengthFieldCorrect)
{
    auto msg = make_rpc_req("add", "id_x");
    std::string raw = proto->serialize(msg);

    int32_t len_field;
    std::memcpy(&len_field, raw.data(), 4);
    len_field = static_cast<int32_t>(::ntohl(static_cast<uint32_t>(len_field)));

    EXPECT_EQ(static_cast<size_t>(len_field) + 4, raw.size());
}

// -------------------- 各消息类型序列化后 length 均正确 --------------------

TEST_F(LVProtocolTest, Serialize_AllTypes_LengthFieldCorrect)
{
    std::vector<BaseMessage::ptr> msgs = {
        make_rpc_req(), make_rpc_rsp(),
        make_topic_req(), make_topic_rsp(),
        make_service_req(), make_service_rsp()};
    for (auto &msg : msgs)
    {
        std::string raw = proto->serialize(msg);
        int32_t len_field;
        std::memcpy(&len_field, raw.data(), 4);
        len_field = static_cast<int32_t>(::ntohl(static_cast<uint32_t>(len_field)));
        EXPECT_EQ(static_cast<size_t>(len_field) + 4, raw.size())
            << "MType=" << static_cast<int>(msg->MType());
    }
}

// -------------------- 超长 Id 往返 --------------------

TEST_F(LVProtocolTest, RoundTrip_LongId)
{
    std::string long_id(1024, 'x');
    auto msg = make_rpc_req("add", long_id);
    auto buf = make_buf(proto->serialize(msg));

    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));
    EXPECT_EQ(out->Id(), long_id);
}

// -------------------- 连续 N 条消息批量解析 --------------------

TEST_F(LVProtocolTest, Sticky_NMessagesInOneBuffer)
{
    const int N = 10;
    std::string raw;
    for (int i = 0; i < N; ++i)
        raw += proto->serialize(make_rpc_req("fn", "id_" + std::to_string(i)));

    auto buf = make_buf(raw);
    for (int i = 0; i < N; ++i)
    {
        ASSERT_TRUE(proto->canProcessed(buf)) << "i=" << i;
        BaseMessage::ptr out;
        ASSERT_TRUE(proto->onMessage(buf, out)) << "i=" << i;
        EXPECT_EQ(out->Id(), "id_" + std::to_string(i));
    }
    EXPECT_FALSE(proto->canProcessed(buf));
    EXPECT_EQ(buf->readableBytes(), 0u);
}

// -------------------- 解析后缓冲区恰好清空 --------------------

TEST_F(LVProtocolTest, OnMessage_BufferExactlyConsumed)
{
    auto msg = make_rpc_req("fn", "id_consume");
    auto buf = make_buf(proto->serialize(msg));

    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));
    EXPECT_EQ(buf->readableBytes(), 0u);
}

// -------------------- 特殊字符 Id --------------------

TEST_F(LVProtocolTest, RoundTrip_SpecialCharId)
{
    std::string special_id = "id/with\\special:chars?&=+";
    auto msg = make_rpc_req("fn", special_id);
    auto buf = make_buf(proto->serialize(msg));

    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));
    EXPECT_EQ(out->Id(), special_id);
}

// -------------------- 同一消息序列化结果可被多次独立解析 --------------------

TEST_F(LVProtocolTest, Serialize_MultipleIndependentParse)
{
    auto msg = make_rpc_req("fn", "shared_id");
    std::string raw = proto->serialize(msg);

    for (int i = 0; i < 5; ++i)
    {
        auto buf = make_buf(raw);
        BaseMessage::ptr out;
        ASSERT_TRUE(proto->onMessage(buf, out)) << "i=" << i;
        EXPECT_EQ(out->Id(), "shared_id");
        EXPECT_EQ(out->MType(), MType::REQ_RPC);
    }
}

// -------------------- RpcResponse 错误码往返 --------------------

// TEST_F(LVProtocolTest, RoundTrip_RpcResponse_ErrorCode)
// {
//     auto msg = MessageFactory::create<RpcResponse>();
//     auto rsp = std::static_pointer_cast<RpcResponse>(msg);
//     rsp->setRcode(RCode::RCODE_NOT_FOUND);
//     rsp->setResult(Json::Value());
//     rsp->setId("err_id");
//     rsp->setMType(MType::RSP_RPC);

//     auto buf = make_buf(proto->serialize(msg));
//     BaseMessage::ptr out;
//     ASSERT_TRUE(proto->onMessage(buf, out));

//     auto out_rsp = std::dynamic_pointer_cast<RpcResponse>(out);
//     ASSERT_NE(out_rsp, nullptr);
//     EXPECT_EQ(out_rsp->rcode(), RCode::RCODE_NOT_FOUND);
// }

// -------------------- ServiceResponse 空 hosts 往返 --------------------

TEST_F(LVProtocolTest, RoundTrip_ServiceResponse_EmptyHosts)
{
    auto msg = MessageFactory::create<ServiceResponse>();
    auto rsp = std::static_pointer_cast<ServiceResponse>(msg);
    rsp->setRcode(RCode::RCODE_OK);
    rsp->setMethod("no_hosts");
    rsp->setServiceOptype(ServiceOptype::SERVICE_DISCOVERY);
    rsp->setHosts({});
    rsp->setId("eh_id");
    rsp->setMType(MType::RSP_SERVICE);

    auto buf = make_buf(proto->serialize(msg));
    BaseMessage::ptr out;
    ASSERT_TRUE(proto->onMessage(buf, out));

    auto out_rsp = std::dynamic_pointer_cast<ServiceResponse>(out);
    ASSERT_NE(out_rsp, nullptr);
    EXPECT_TRUE(out_rsp->hosts().empty());
}

// -------------------- 篡改 body 导致反序列化失败 --------------------

TEST_F(LVProtocolTest, OnMessage_CorruptedBody_ReturnsFalse)
{
    std::string raw = proto->serialize(make_rpc_req());
    // 将 body 部分全部替换为非 JSON 乱码
    // body 从第 12 字节开始（4+4+4），但要跳过 idlen+id
    // 直接破坏最后 10 字节
    size_t corrupt_start = raw.size() - 10;
    for (size_t i = corrupt_start; i < raw.size(); ++i)
        raw[i] = '\xFF';

    auto buf = make_buf(raw);
    BaseMessage::ptr out;
    EXPECT_FALSE(proto->onMessage(buf, out));
}

// ==================== 集成测试：MuduoServer + MuduoClient ====================
// 注意：muduo EventLoop 必须在同一线程中构造和运行，
// 因此 server/client 必须完全在子线程内构造+启动。

class NetIntegrationTest : public ::testing::Test
{
protected:
    static int next_port()
    {
        static std::atomic<int> port{19900};
        return port.fetch_add(1);
    }

    static bool wait_for(std::atomic<bool> &flag, int timeout_ms = 2000)
    {
        for (int i = 0; i < timeout_ms / 10; ++i)
        {
            if (flag.load())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    static bool wait_for_count(std::atomic<int> &cnt, int target, int timeout_ms = 2000)
    {
        for (int i = 0; i < timeout_ms / 10; ++i)
        {
            if (cnt.load() >= target)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
};

// -------------------- 基本连通性 --------------------

TEST_F(NetIntegrationTest, ConnectAndDisconnect)
{
    int port = next_port();
    std::atomic<bool> conn_called{false};

    // server 必须完全在子线程中构造+启动
    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setConnectionCallback([&](BaseConnection::ptr) {
            conn_called.store(true);
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::thread cli_thread([&]
                           {
        auto client = ClientFactory::create("127.0.0.1", port);
        client->connect();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        client->shutdown(); });
    cli_thread.detach();

    EXPECT_TRUE(wait_for(conn_called)) << "server 未触发 conn_callback";
}

// -------------------- 客户端断开被 server 检测到 --------------------

TEST_F(NetIntegrationTest, ClientShutdown_ServerDetects)
{
    int port = next_port();
    std::atomic<bool> close_called{false};

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setCloseback([&](BaseConnection::ptr) {
            close_called.store(true);
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::thread cli_thread([&]
                           {
        auto client = ClientFactory::create("127.0.0.1", port);
        client->connect();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client->shutdown(); });
    cli_thread.detach();

    EXPECT_TRUE(wait_for(close_called)) << "server 未检测到客户端断开";
}

// -------------------- Client → Server 单条消息 --------------------

TEST_F(NetIntegrationTest, ClientSend_ServerReceives_RpcRequest)
{
    int port = next_port();
    std::atomic<bool> received{false};
    std::string got_method;

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setMessageCallback([&](BaseConnection::ptr, BaseMessage::ptr msg) {
            auto req = std::dynamic_pointer_cast<RpcRequest>(msg);
            if (req) got_method = req->method();
            received.store(true);
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::thread cli_thread([&]
                           {
        auto client = ClientFactory::create("127.0.0.1", port);
        client->connect();
        client->send(make_rpc_req("multiply", "req_x"));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        client->shutdown(); });
    cli_thread.detach();

    ASSERT_TRUE(wait_for(received)) << "server 未收到消息";
    EXPECT_EQ(got_method, "multiply");
}

// -------------------- Server → Client 回复 --------------------

TEST_F(NetIntegrationTest, ServerReply_ClientReceives_RpcResponse)
{
    int port = next_port();
    std::atomic<bool> srv_received{false};
    std::atomic<bool> cli_received{false};
    int got_answer = -1;

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setMessageCallback([&](BaseConnection::ptr conn, BaseMessage::ptr msg) {
            srv_received.store(true);
            conn->send(make_rpc_rsp(777, msg->Id()));
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::thread cli_thread([&]
                           {
        auto client = ClientFactory::create("127.0.0.1", port);
        client->setMessageCallback([&](BaseConnection::ptr, BaseMessage::ptr msg) {
            auto rsp = std::dynamic_pointer_cast<RpcResponse>(msg);
            if (rsp) got_answer = rsp->result()["answer"].asInt();
            cli_received.store(true);
        });
        client->connect();
        client->send(make_rpc_req("add", "echo_id"));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        client->shutdown(); });
    cli_thread.detach();

    ASSERT_TRUE(wait_for(srv_received)) << "server 未收到请求";
    ASSERT_TRUE(wait_for(cli_received)) << "client 未收到回复";
    EXPECT_EQ(got_answer, 777);
}

// -------------------- Id 在请求/回复中透传 --------------------

TEST_F(NetIntegrationTest, MessageId_EchoedByServer)
{
    int port = next_port();
    std::string echoed_id;
    std::atomic<bool> cli_received{false};

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setMessageCallback([&](BaseConnection::ptr conn, BaseMessage::ptr msg) {
            conn->send(make_rpc_rsp(0, msg->Id()));
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::thread cli_thread([&]
                           {
        auto client = ClientFactory::create("127.0.0.1", port);
        client->setMessageCallback([&](BaseConnection::ptr, BaseMessage::ptr msg) {
            echoed_id = msg->Id();
            cli_received.store(true);
        });
        client->connect();
        client->send(make_rpc_req("f", "unique_id_12345"));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        client->shutdown(); });
    cli_thread.detach();

    ASSERT_TRUE(wait_for(cli_received)) << "client 未收到回复";
    EXPECT_EQ(echoed_id, "unique_id_12345");
}

// -------------------- 连续发送多条消息（粘包场景） --------------------

TEST_F(NetIntegrationTest, MultipleMessages_AllReceived)
{
    const int N = 5;
    int port = next_port();
    std::atomic<int> count{0};

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setMessageCallback([&](BaseConnection::ptr, BaseMessage::ptr) {
            count.fetch_add(1);
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::thread cli_thread([&]
                           {
        auto client = ClientFactory::create("127.0.0.1", port);
        client->connect();
        for (int i = 0; i < N; ++i)
            client->send(make_rpc_req("fn_" + std::to_string(i), "id_" + std::to_string(i)));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        client->shutdown(); });
    cli_thread.detach();

    ASSERT_TRUE(wait_for_count(count, N)) << "server 未收到全部消息，收到=" << count.load();
    EXPECT_EQ(count.load(), N);
}

// -------------------- Topic 消息收发 --------------------

TEST_F(NetIntegrationTest, TopicMessage_ServerReceives)
{
    int port = next_port();
    std::atomic<bool> received{false};
    std::string got_topic;

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setMessageCallback([&](BaseConnection::ptr, BaseMessage::ptr msg) {
            auto req = std::dynamic_pointer_cast<TopicRequest>(msg);
            if (req) got_topic = req->topic();
            received.store(true);
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::thread cli_thread([&]
                           {
        auto client = ClientFactory::create("127.0.0.1", port);
        client->connect();
        client->send(make_topic_req("finance", "tp_x"));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        client->shutdown(); });
    cli_thread.detach();

    ASSERT_TRUE(wait_for(received));
    EXPECT_EQ(got_topic, "finance");
}

// -------------------- Service 消息收发 --------------------

TEST_F(NetIntegrationTest, ServiceMessage_ServerReceives)
{
    int port = next_port();
    std::atomic<bool> received{false};
    std::string got_method;

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setMessageCallback([&](BaseConnection::ptr, BaseMessage::ptr msg) {
            auto req = std::dynamic_pointer_cast<ServiceRequest>(msg);
            if (req) got_method = req->method();
            received.store(true);
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::thread cli_thread([&]
                           {
        auto client = ClientFactory::create("127.0.0.1", port);
        client->connect();
        client->send(make_service_req("register_svc", "sr_x"));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        client->shutdown(); });
    cli_thread.detach();

    ASSERT_TRUE(wait_for(received));
    EXPECT_EQ(got_method, "register_svc");
}

// -------------------- 多客户端同时连接 --------------------

TEST_F(NetIntegrationTest, MultipleClients_AllConnected)
{
    const int CLIENTS = 3;
    int port = next_port();
    std::atomic<int> conn_count{0};

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setConnectionCallback([&](BaseConnection::ptr) {
            conn_count.fetch_add(1);
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<std::thread> cli_threads;
    for (int i = 0; i < CLIENTS; ++i)
    {
        cli_threads.emplace_back([&]
                                 {
            auto c = ClientFactory::create("127.0.0.1", port);
            c->connect();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            c->shutdown(); });
    }
    for (auto &t : cli_threads)
        t.detach();

    ASSERT_TRUE(wait_for_count(conn_count, CLIENTS)) << "未收到全部连接";
    EXPECT_EQ(conn_count.load(), CLIENTS);
}

// -------------------- 多客户端独立收发，消息不串扰 --------------------

TEST_F(NetIntegrationTest, MultipleClients_IndependentMessages)
{
    const int CLIENTS = 3;
    int port = next_port();

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setMessageCallback([&](BaseConnection::ptr conn, BaseMessage::ptr msg) {
            conn->send(make_rpc_rsp(0, msg->Id()));
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<std::string> received_ids(CLIENTS);
    std::vector<std::atomic<bool>> flags(CLIENTS);
    for (int i = 0; i < CLIENTS; ++i)
        flags[i].store(false);

    std::vector<std::thread> cli_threads;
    for (int i = 0; i < CLIENTS; ++i)
    {
        cli_threads.emplace_back([&, i]
                                 {
            auto c = ClientFactory::create("127.0.0.1", port);
            c->setMessageCallback([&, i](BaseConnection::ptr, BaseMessage::ptr msg) {
                received_ids[i] = msg->Id();
                flags[i].store(true);
            });
            c->connect();
            c->send(make_rpc_req("fn", "client_id_" + std::to_string(i)));
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            c->shutdown(); });
    }
    for (auto &t : cli_threads)
        t.detach();

    for (int i = 0; i < CLIENTS; ++i)
    {
        ASSERT_TRUE(wait_for(flags[i])) << "client " << i << " 未收到回复";
        EXPECT_EQ(received_ids[i], "client_id_" + std::to_string(i));
    }
}

// -------------------- 服务端连接/断开计数对称 --------------------

TEST_F(NetIntegrationTest, ConnClose_CountSymmetric)
{
    const int CLIENTS = 4;
    int port = next_port();
    std::atomic<int> conn_count{0};
    std::atomic<int> close_count{0};

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setConnectionCallback([&](BaseConnection::ptr) { conn_count.fetch_add(1); });
        server->setCloseback([&](BaseConnection::ptr) { close_count.fetch_add(1); });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    for (int i = 0; i < CLIENTS; ++i)
    {
        std::thread([&]
                    {
            auto c = ClientFactory::create("127.0.0.1", port);
            c->connect();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            c->shutdown(); })
            .detach();
    }

    ASSERT_TRUE(wait_for_count(close_count, CLIENTS, 3000));
    EXPECT_EQ(conn_count.load(), CLIENTS);
    EXPECT_EQ(close_count.load(), CLIENTS);
}

// -------------------- 大消息（16KB body）往返 --------------------

TEST_F(NetIntegrationTest, LargeMessage_RoundTrip)
{
    int port = next_port();
    std::atomic<bool> received{false};
    std::string got_param;

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setMessageCallback([&](BaseConnection::ptr conn, BaseMessage::ptr msg) {
            auto req = std::dynamic_pointer_cast<RpcRequest>(msg);
            if (req) got_param = req->param()["data"].asString();
            conn->send(make_rpc_rsp(0, msg->Id()));
            received.store(true);
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string big(1 << 14, 'z');
    std::thread cli_thread([&]
                           {
        auto msg2 = MessageFactory::create<RpcRequest>();
        auto req  = std::static_pointer_cast<RpcRequest>(msg2);
        Json::Value params;
        params["data"] = big;
        req->setMethod("bigcall");
        req->setParam(params);
        req->setId("big_id");
        req->setMType(MType::REQ_RPC);

        auto client = ClientFactory::create("127.0.0.1", port);
        client->connect();
        client->send(msg2);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        client->shutdown(); });
    cli_thread.detach();

    ASSERT_TRUE(wait_for(received, 3000)) << "server 未收到大消息";
    EXPECT_EQ(got_param, big);
}

// -------------------- 服务端广播：每个客户端均收到消息 --------------------

TEST_F(NetIntegrationTest, ServerBroadcast_AllClientsReceive)
{
    const int CLIENTS = 3;
    int port = next_port();

    std::mutex conns_mutex;
    std::vector<BaseConnection::ptr> server_conns;
    std::atomic<int> conn_count{0};

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(port);
        server->setConnectionCallback([&](BaseConnection::ptr conn) {
            std::lock_guard<std::mutex> lk(conns_mutex);
            server_conns.push_back(conn);
            conn_count.fetch_add(1);
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<std::atomic<bool>> received(CLIENTS);
    for (int i = 0; i < CLIENTS; ++i)
        received[i].store(false);

    for (int i = 0; i < CLIENTS; ++i)
    {
        std::thread([&, i]
                    {
            auto c = ClientFactory::create("127.0.0.1", port);
            c->setMessageCallback([&, i](BaseConnection::ptr, BaseMessage::ptr) {
                received[i].store(true);
            });
            c->connect();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            c->shutdown(); })
            .detach();
    }

    // 等待所有客户端连接完成
    ASSERT_TRUE(wait_for_count(conn_count, CLIENTS));

    {
        std::lock_guard<std::mutex> lk(conns_mutex);
        for (auto &conn : server_conns)
            conn->send(make_rpc_rsp(100, "bcast"));
    }

    for (int i = 0; i < CLIENTS; ++i)
        EXPECT_TRUE(wait_for(received[i])) << "client " << i << " 未收到广播";
}

// ==================== 主函数 ====================

int main(int argc, char **argv)
{
    Logger::instance().init(true, "log/log.log", spdlog::level::info);

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}