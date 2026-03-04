#include <gtest/gtest.h>
#include <json/json.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "../../client/client.hpp"
#include "../../server/server.hpp"
#include "../../common/message.hpp"
#include "../../common/net.hpp"

using namespace Lyuih;

namespace
{
    int next_port()
    {
        static std::atomic<int> p{21000};
        return p.fetch_add(1);
    }

    bool wait_for(const std::function<bool()> &pred, int timeout_ms = 3000, int step_ms = 20)
    {
        int waited = 0;
        while (waited < timeout_ms)
        {
            if (pred())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
            waited += step_ms;
        }
        return pred();
    }
} // namespace

// 1) 直连 RPC 调用
TEST(RpcUsageTest, DirectRpcCall_WithoutDiscovery)
{
    int rpc_port = next_port();

    std::thread srv([&]
                    {
        auto server = ServerFactory::create(rpc_port);
        server->setMessageCallback([&](BaseConnection::ptr conn, BaseMessage::ptr msg) {
            auto req = std::dynamic_pointer_cast<RpcRequest>(msg);
            if (!req) return;

            Json::Value result;
            int a = req->param().isMember("a") ? req->param()["a"].asInt() : 0;
            int b = req->param().isMember("b") ? req->param()["b"].asInt() : 0;
            result["sum"] = a + b;

            auto rsp = MessageFactory::create<RpcResponse>();
            rsp->setRcode(RCode::RCODE_OK);
            rsp->setResult(result);
            rsp->setId(req->Id());
            rsp->setMType(MType::RSP_RPC);
            conn->send(rsp);
        });
        server->start(); });
    srv.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    client::RpcClient cli(false, "127.0.0.1", rpc_port);

    Json::Value params, result;
    params["a"] = 7;
    params["b"] = 8;

    ASSERT_TRUE(cli.call("add", params, result));
    EXPECT_EQ(result["sum"].asInt(), 15);
}

// 2) 基于服务注册发现的 RPC 调用
TEST(RpcUsageTest, RpcCall_WithRegistryAndDiscovery)
{
    int reg_port = next_port();
    int rpc_port = next_port();

    // 注册中心
    std::thread reg_srv([&]
                        {
        server::RegistryServer registry(reg_port);
        registry.start(); });
    reg_srv.detach();

    // 服务提供者（简化版 RPC 服务端）
    std::thread provider_srv([&]
                             {
        auto server = ServerFactory::create(rpc_port);
        server->setMessageCallback([&](BaseConnection::ptr conn, BaseMessage::ptr msg) {
            auto req = std::dynamic_pointer_cast<RpcRequest>(msg);
            if (!req) return;

            Json::Value result;
            int x = req->param().isMember("x") ? req->param()["x"].asInt() : 0;
            int y = req->param().isMember("y") ? req->param()["y"].asInt() : 0;
            result["mul"] = x * y;

            auto rsp = MessageFactory::create<RpcResponse>();
            rsp->setRcode(RCode::RCODE_OK);
            rsp->setResult(result);
            rsp->setId(req->Id());
            rsp->setMType(MType::RSP_RPC);
            conn->send(rsp);
        });
        server->start(); });
    provider_srv.detach();

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 服务注册
    client::RegistryClient registry_cli("127.0.0.1", reg_port);
    ASSERT_TRUE(registry_cli.registryMethod("mul", {"127.0.0.1", rpc_port}));

    // 服务发现 + RPC 调用
    client::RpcClient consumer(true, "127.0.0.1", reg_port);

    Json::Value params, result;
    params["x"] = 6;
    params["y"] = 9;

    ASSERT_TRUE(consumer.call("mul", params, result));
    EXPECT_EQ(result["mul"].asInt(), 54);
}

// 3) 基于广播的发布订阅
TEST(TopicUsageTest, BroadcastPubSub_AllSubscribersReceive)
{
    int topic_port = next_port();

    std::thread topic_srv([&]
                          {
        server::TopicServer srv(topic_port);
        srv.start(); });
    topic_srv.detach();

    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    client::TopicClient publisher("127.0.0.1", topic_port);
    client::TopicClient sub1("127.0.0.1", topic_port);
    client::TopicClient sub2("127.0.0.1", topic_port);
    client::TopicClient sub3("127.0.0.1", topic_port);

    ASSERT_TRUE(publisher.create("news"));

    std::atomic<int> recv_count{0};

    client::TopicManager::SubCallback cb = [&](auto &&...)
    {
        recv_count.fetch_add(1);
    };

    ASSERT_TRUE(sub1.subscribe("news", cb));
    ASSERT_TRUE(sub2.subscribe("news", cb));
    ASSERT_TRUE(sub3.subscribe("news", cb));

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_TRUE(publisher.publish("news", "hello-all"));

    ASSERT_TRUE(wait_for([&]
                         { return recv_count.load() >= 3; }, 4000))
        << "广播消息未被所有订阅者收到";

    sub1.shutdown();
    sub2.shutdown();
    sub3.shutdown();
    publisher.shutdown();
}

