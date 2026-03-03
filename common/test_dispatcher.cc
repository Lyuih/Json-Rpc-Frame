#include <gtest/gtest.h>
#include <memory>
#include <atomic>
#include <string>
#include <vector>
#include <thread>
#include <mutex>

#include "dispatcher.hpp"
#include "message.hpp"
#include "fields.hpp"
#include "log.hpp"
#include "abstract.hpp"

using namespace Lyuih;

// ==================== MockConnection ====================

class MockConnection : public BaseConnection
{
public:
    using ptr = std::shared_ptr<MockConnection>;
    MockConnection() : shutdown_called_(false), connected_(true) {}

    void send(const BaseMessage::ptr &) override {}

    void shutdown() override
    {
        connected_ = false;
        shutdown_called_ = true;
    }

    bool connected() override { return connected_; }

    bool shutdownCalled() const { return shutdown_called_; }

private:
    std::atomic<bool> shutdown_called_;
    std::atomic<bool> connected_;
};

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
    req->setHost({"127.0.0.1", 8080});
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
    rsp->setHosts({{"127.0.0.1", 8080}});
    rsp->setId(id);
    rsp->setMType(MType::RSP_SERVICE);
    return msg;
}

// ==================== 测试夹具 ====================

class DispatcherTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        dispatcher_ = std::make_shared<Dispatcher>();
        conn_ = std::make_shared<MockConnection>();
    }

    Dispatcher::ptr dispatcher_;
    MockConnection::ptr conn_;
};

// ==================== CallBackT 单元测试 ====================

// 回调被正确调用，conn 和 msg 均正确传入
TEST(CallBackTTest, Invoke_CorrectConnAndMsg)
{
    BaseConnection::ptr got_conn;
    std::shared_ptr<RpcRequest> got_msg;

    auto cb = std::make_shared<CallBackT<RpcRequest>>(
        [&](const BaseConnection::ptr &c, const std::shared_ptr<RpcRequest> &m)
        {
            got_conn = c;
            got_msg = m;
        });

    auto conn = std::make_shared<MockConnection>();
    auto msg = make_rpc_req("fn", "id_x");

    cb->onMesssage(conn, msg);

    EXPECT_EQ(got_conn, conn);
    ASSERT_NE(got_msg, nullptr);
    EXPECT_EQ(got_msg->method(), "fn");
    EXPECT_EQ(got_msg->Id(), "id_x");
}

// 空回调不崩溃
TEST(CallBackTTest, NullCallback_NoCrash)
{
    auto cb = std::make_shared<CallBackT<RpcRequest>>(nullptr);
    auto conn = std::make_shared<MockConnection>();
    EXPECT_NO_FATAL_FAILURE(cb->onMesssage(conn, make_rpc_req()));
}

// 消息类型转换正确（RpcResponse → RpcResponse）
TEST(CallBackTTest, TypeCast_RpcResponse_Correct)
{
    int got_answer = -1;
    auto cb = std::make_shared<CallBackT<RpcResponse>>(
        [&](const BaseConnection::ptr &, const std::shared_ptr<RpcResponse> &m)
        {
            if (m)
                got_answer = m->result()["answer"].asInt();
        });

    cb->onMesssage(std::make_shared<MockConnection>(), make_rpc_rsp(77));
    EXPECT_EQ(got_answer, 77);
}

// 错误类型转换（传入 RpcRequest 给 RpcResponse 回调）→ dynamic_pointer_cast 为 nullptr
TEST(CallBackTTest, TypeCast_WrongType_NullPtr)
{
    std::shared_ptr<RpcResponse> got_msg = reinterpret_cast<RpcResponse *>(1) == nullptr
                                               ? nullptr
                                               : nullptr; // 初始化规避警告
    bool called = false;
    auto cb = std::make_shared<CallBackT<RpcResponse>>(
        [&](const BaseConnection::ptr &, const std::shared_ptr<RpcResponse> &m)
        {
            called = true;
            got_msg = m; // 期望为 nullptr（dynamic_pointer_cast 失败）
        });

    cb->onMesssage(std::make_shared<MockConnection>(), make_rpc_req());

    EXPECT_TRUE(called);         // 回调仍被调用
    EXPECT_EQ(got_msg, nullptr); // 但 msg 转换结果为 nullptr
}

// ==================== Dispatcher 基础分发测试 ====================

// 注册并分发 RpcRequest
TEST_F(DispatcherTest, Dispatch_RpcRequest_CallbackInvoked)
{
    bool called = false;
    dispatcher_->registerHandler<RpcRequest>(
        MType::REQ_RPC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<RpcRequest> &)
        {
            called = true;
        });

    dispatcher_->onMesssage(conn_, make_rpc_req());
    EXPECT_TRUE(called);
}

// 注册并分发 RpcResponse
TEST_F(DispatcherTest, Dispatch_RpcResponse_CallbackInvoked)
{
    bool called = false;
    dispatcher_->registerHandler<RpcResponse>(
        MType::RSP_RPC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<RpcResponse> &)
        {
            called = true;
        });

    dispatcher_->onMesssage(conn_, make_rpc_rsp());
    EXPECT_TRUE(called);
}

// 注册并分发 TopicRequest
TEST_F(DispatcherTest, Dispatch_TopicRequest_CallbackInvoked)
{
    bool called = false;
    dispatcher_->registerHandler<TopicRequest>(
        MType::REQ_TOPIC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<TopicRequest> &m)
        {
            called = (m != nullptr && m->topic() == "finance");
        });

    dispatcher_->onMesssage(conn_, make_topic_req("finance", "id_1"));
    EXPECT_TRUE(called);
}

// 注册并分发 TopicResponse
TEST_F(DispatcherTest, Dispatch_TopicResponse_CallbackInvoked)
{
    bool called = false;
    dispatcher_->registerHandler<TopicResponse>(
        MType::RSP_TOPIC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<TopicResponse> &)
        {
            called = true;
        });

    dispatcher_->onMesssage(conn_, make_topic_rsp());
    EXPECT_TRUE(called);
}

// 注册并分发 ServiceRequest
TEST_F(DispatcherTest, Dispatch_ServiceRequest_CallbackInvoked)
{
    bool called = false;
    dispatcher_->registerHandler<ServiceRequest>(
        MType::REQ_SERVICE,
        [&](const BaseConnection::ptr &, const std::shared_ptr<ServiceRequest> &m)
        {
            called = (m != nullptr && m->method() == "calc");
        });

    dispatcher_->onMesssage(conn_, make_service_req());
    EXPECT_TRUE(called);
}

// 注册并分发 ServiceResponse
TEST_F(DispatcherTest, Dispatch_ServiceResponse_CallbackInvoked)
{
    bool called = false;
    dispatcher_->registerHandler<ServiceResponse>(
        MType::RSP_SERVICE,
        [&](const BaseConnection::ptr &, const std::shared_ptr<ServiceResponse> &m)
        {
            called = (m != nullptr);
        });

    dispatcher_->onMesssage(conn_, make_service_rsp());
    EXPECT_TRUE(called);
}

// ==================== Dispatcher conn 透传测试 ====================

// 回调中收到的 conn 与分发时一致
TEST_F(DispatcherTest, Dispatch_ConnPassedThrough)
{
    BaseConnection::ptr got_conn;
    dispatcher_->registerHandler<RpcRequest>(
        MType::REQ_RPC,
        [&](const BaseConnection::ptr &c, const std::shared_ptr<RpcRequest> &)
        {
            got_conn = c;
        });

    dispatcher_->onMesssage(conn_, make_rpc_req());
    EXPECT_EQ(got_conn, conn_);
}

// 不同连接传入，回调中 conn 对应正确
TEST_F(DispatcherTest, Dispatch_MultipleConns_EachCorrect)
{
    auto conn2 = std::make_shared<MockConnection>();
    std::vector<BaseConnection::ptr> received_conns;
    std::mutex mu;

    dispatcher_->registerHandler<RpcRequest>(
        MType::REQ_RPC,
        [&](const BaseConnection::ptr &c, const std::shared_ptr<RpcRequest> &)
        {
            std::lock_guard<std::mutex> lk(mu);
            received_conns.push_back(c);
        });

    dispatcher_->onMesssage(conn_, make_rpc_req("f1", "id_1"));
    dispatcher_->onMesssage(conn2, make_rpc_req("f2", "id_2"));

    ASSERT_EQ(received_conns.size(), 2u);
    EXPECT_EQ(received_conns[0], conn_);
    EXPECT_EQ(received_conns[1], conn2);
}

// ==================== Dispatcher 消息内容透传测试 ====================

// 回调中收到的 msg 字段与原始消息一致
TEST_F(DispatcherTest, Dispatch_MsgContentPreserved_RpcRequest)
{
    std::string got_method;
    std::string got_id;
    int got_param = -1;

    dispatcher_->registerHandler<RpcRequest>(
        MType::REQ_RPC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<RpcRequest> &m)
        {
            if (!m)
                return;
            got_method = m->method();
            got_id = m->Id();
            got_param = m->param()["a"].asInt();
        });

    dispatcher_->onMesssage(conn_, make_rpc_req("multiply", "msg_007"));

    EXPECT_EQ(got_method, "multiply");
    EXPECT_EQ(got_id, "msg_007");
    EXPECT_EQ(got_param, 1);
}

// TEST_F(DispatcherTest, Dispatch_MsgContentPreserved_RpcResponse)
// {
//     int got_answer = -1;
//     auto rcode = RCode::RCODE_NOT_FOUND;

//     dispatcher_->registerHandler<RpcResponse>(
//         MType::RSP_RPC,
//         [&](const BaseConnection::ptr &, const std::shared_ptr<RpcResponse> &m)
//         {
//             if (!m)
//                 return;
//             got_answer = m->result()["answer"].asInt();
//             rcode = m->rcode();
//         });

//     dispatcher_->onMesssage(conn_, make_rpc_rsp(123, "rsp_x"));

//     EXPECT_EQ(got_answer, 123);
//     EXPECT_EQ(rcode, RCode::RCODE_OK);
// }

// ==================== Dispatcher 无回调处理测试 ====================

// 未注册的消息类型 → 连接被 shutdown，不崩溃
TEST_F(DispatcherTest, Dispatch_UnregisteredType_ShutdownConn)
{
    // 不注册任何回调，直接分发
    dispatcher_->onMesssage(conn_, make_rpc_req());

    EXPECT_TRUE(conn_->shutdownCalled());
    EXPECT_FALSE(conn_->connected());
}

// 注册了 REQ_RPC，但传入 RSP_RPC → 触发 shutdown
TEST_F(DispatcherTest, Dispatch_WrongMType_ShutdownConn)
{
    dispatcher_->registerHandler<RpcRequest>(
        MType::REQ_RPC,
        [](const BaseConnection::ptr &, const std::shared_ptr<RpcRequest> &) {});

    dispatcher_->onMesssage(conn_, make_rpc_rsp()); // RSP_RPC，未注册
    EXPECT_TRUE(conn_->shutdownCalled());
}

// ==================== Dispatcher 覆盖注册测试 ====================

// 同一 MType 注册两次，后注册的回调覆盖前者
TEST_F(DispatcherTest, Register_Overwrite_LastCallbackWins)
{
    int call_order = 0;

    dispatcher_->registerHandler<RpcRequest>(
        MType::REQ_RPC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<RpcRequest> &)
        {
            call_order = 1; // 第一次注册
        });
    dispatcher_->registerHandler<RpcRequest>(
        MType::REQ_RPC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<RpcRequest> &)
        {
            call_order = 2; // 第二次注册（覆盖）
        });

    dispatcher_->onMesssage(conn_, make_rpc_req());
    EXPECT_EQ(call_order, 2);
    EXPECT_FALSE(conn_->shutdownCalled());
}

// ==================== Dispatcher 多类型独立分发测试 ====================

// 注册多种消息类型，各自只触发对应回调
TEST_F(DispatcherTest, Dispatch_MultipleTypes_EachCallsOwnCallback)
{
    std::atomic<int> rpc_count{0};
    std::atomic<int> topic_count{0};
    std::atomic<int> service_count{0};

    dispatcher_->registerHandler<RpcRequest>(
        MType::REQ_RPC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<RpcRequest> &)
        {
            rpc_count.fetch_add(1);
        });
    dispatcher_->registerHandler<TopicRequest>(
        MType::REQ_TOPIC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<TopicRequest> &)
        {
            topic_count.fetch_add(1);
        });
    dispatcher_->registerHandler<ServiceRequest>(
        MType::REQ_SERVICE,
        [&](const BaseConnection::ptr &, const std::shared_ptr<ServiceRequest> &)
        {
            service_count.fetch_add(1);
        });

    dispatcher_->onMesssage(conn_, make_rpc_req());
    dispatcher_->onMesssage(conn_, make_topic_req());
    dispatcher_->onMesssage(conn_, make_service_req());

    EXPECT_EQ(rpc_count.load(), 1);
    EXPECT_EQ(topic_count.load(), 1);
    EXPECT_EQ(service_count.load(), 1);
}

// ==================== Dispatcher 连续分发测试 ====================

// 同一类型连续分发 N 次，回调被调用 N 次
TEST_F(DispatcherTest, Dispatch_SameType_NTimes)
{
    const int N = 10;
    std::atomic<int> count{0};

    dispatcher_->registerHandler<RpcRequest>(
        MType::REQ_RPC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<RpcRequest> &)
        {
            count.fetch_add(1);
        });

    for (int i = 0; i < N; ++i)
        dispatcher_->onMesssage(conn_, make_rpc_req("fn", "id_" + std::to_string(i)));

    EXPECT_EQ(count.load(), N);
}

// ==================== Dispatcher 并发安全测试 ====================

// 多线程并发 registerHandler 和 onMessage，不崩溃，最终回调计数正确
TEST_F(DispatcherTest, Concurrency_RegisterAndDispatch_NoCrash)
{
    const int THREADS = 4;
    const int MSG_PER_T = 50;
    std::atomic<int> count{0};

    // 先注册好回调
    dispatcher_->registerHandler<RpcRequest>(
        MType::REQ_RPC,
        [&](const BaseConnection::ptr &, const std::shared_ptr<RpcRequest> &)
        {
            count.fetch_add(1);
        });

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back([&]
                             {
            for (int i = 0; i < MSG_PER_T; ++i)
            {
                auto conn = std::make_shared<MockConnection>();
                dispatcher_->onMesssage(conn, make_rpc_req());
            } });
    }
    for (auto &th : threads)
        th.join();

    EXPECT_EQ(count.load(), THREADS * MSG_PER_T);
}

// 多线程并发注册不同类型，不崩溃
TEST_F(DispatcherTest, Concurrency_MultiTypeRegister_NoCrash)
{
    std::vector<std::thread> threads;

    threads.emplace_back([&]
                         {
        for (int i = 0; i < 20; ++i)
            dispatcher_->registerHandler<RpcRequest>(
                MType::REQ_RPC,
                [](const BaseConnection::ptr &, const std::shared_ptr<RpcRequest> &) {}); });
    threads.emplace_back([&]
                         {
        for (int i = 0; i < 20; ++i)
            dispatcher_->registerHandler<TopicRequest>(
                MType::REQ_TOPIC,
                [](const BaseConnection::ptr &, const std::shared_ptr<TopicRequest> &) {}); });
    threads.emplace_back([&]
                         {
        for (int i = 0; i < 20; ++i)
        {
            auto conn = std::make_shared<MockConnection>();
            dispatcher_->onMesssage(conn, make_rpc_req());
        } });

    EXPECT_NO_FATAL_FAILURE(for (auto &th : threads) th.join());
}

// ==================== 主函数 ====================

int main(int argc, char **argv)
{
    Logger::instance().init(false, "", spdlog::level::warn);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}