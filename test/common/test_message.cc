#include <gtest/gtest.h>
#include <json/json.h>
#include <memory>
#include <string>
#include <vector>
#include <sstream>

#include "../../common/fields.hpp"
#include "../../common/message.hpp"

using namespace Lyuih;

// ==================== 辅助工具 ====================

// 构造一个所有字段都合法的基础 JSON，通过 override_key/remove 精确注入错误
static std::string make_json(const std::string &override_key = "",
                              const std::string &override_val = "",
                              bool remove = false)
{
    Json::Value root;
    root[KEY_METHOD]    = "test_method";
    root[KEY_PARAMS]    = Json::Value(Json::objectValue);
    root[KEY_RCODE]     = 0;
    root[KEY_OPTYPE]    = 0;
    root[KEY_TOPIC_KEY] = "test_topic";
    root[KEY_TOPIC_MSG] = "test_msg";

    Json::Value host_val;
    host_val[KEY_HOST_IP]   = "127.0.0.1";
    host_val[KEY_HOST_PORT] = 8080;
    root[KEY_HOST] = host_val;

    Json::Value result_val;
    result_val["value"] = 0;
    root[KEY_RESULT] = result_val;

    if (!override_key.empty()) {
        if (remove) {
            root.removeMember(override_key);
        } else {
            Json::Value v;
            Json::CharReaderBuilder rb;
            std::string errs;
            std::istringstream iss(override_val);
            Json::parseFromStream(rb, iss, &v, &errs);
            root[override_key] = v;
        }
    }

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, root);
}

// ==================== MessageFactory 测试 ====================

TEST(MessageFactoryTest, CreateByMType_AllTypes)
{
    struct Case { MType type; const char* name; };
    Case cases[] = {
        {MType::REQ_RPC,     "REQ_RPC"},
        {MType::RSP_RPC,     "RSP_RPC"},
        {MType::REQ_TOPIC,   "REQ_TOPIC"},
        {MType::RSP_TOPIC,   "RSP_TOPIC"},
        {MType::REQ_SERVICE, "REQ_SERVICE"},
        {MType::RSP_SERVICE, "RSP_SERVICE"},
    };
    for (auto &c : cases) {
        auto msg = MessageFactory::create(c.type);
        EXPECT_NE(msg, nullptr) << "failed: " << c.name;
    }
}

TEST(MessageFactoryTest, CreateByMType_CorrectDynamicType)
{
    EXPECT_NE(dynamic_cast<RpcRequest*>     (MessageFactory::create(MType::REQ_RPC).get()),     nullptr);
    EXPECT_NE(dynamic_cast<RpcResponse*>    (MessageFactory::create(MType::RSP_RPC).get()),     nullptr);
    EXPECT_NE(dynamic_cast<TopicRequest*>   (MessageFactory::create(MType::REQ_TOPIC).get()),   nullptr);
    EXPECT_NE(dynamic_cast<TopicResponse*>  (MessageFactory::create(MType::RSP_TOPIC).get()),   nullptr);
    EXPECT_NE(dynamic_cast<ServiceRequest*> (MessageFactory::create(MType::REQ_SERVICE).get()), nullptr);
    EXPECT_NE(dynamic_cast<ServiceResponse*>(MessageFactory::create(MType::RSP_SERVICE).get()), nullptr);
}

TEST(MessageFactoryTest, CreateByMType_InvalidReturnsNull)
{
    EXPECT_EQ(MessageFactory::create(static_cast<MType>(999)), nullptr);
}

TEST(MessageFactoryTest, CreateByTemplate)
{
    EXPECT_NE(MessageFactory::create<RpcRequest>(),      nullptr);
    EXPECT_NE(MessageFactory::create<RpcResponse>(),     nullptr);
    EXPECT_NE(MessageFactory::create<TopicRequest>(),    nullptr);
    EXPECT_NE(MessageFactory::create<TopicResponse>(),   nullptr);
    EXPECT_NE(MessageFactory::create<ServiceRequest>(),  nullptr);
    EXPECT_NE(MessageFactory::create<ServiceResponse>(), nullptr);
}

// ==================== RpcRequest 测试 ====================

TEST(RpcRequestTest, InitialCheckFails)
{
    EXPECT_FALSE(MessageFactory::create<RpcRequest>()->check());
}

TEST(RpcRequestTest, ValidFieldsCheckPass)
{
    auto req = MessageFactory::create<RpcRequest>();
    Json::Value params;
    params["a"] = 1;
    req->setMethod("add");
    req->setParam(params);
    EXPECT_EQ(req->method(), "add");
    EXPECT_EQ(req->param(), params);
    EXPECT_TRUE(req->check());
}

// TEST(RpcRequestTest, EmptyMethodCheckFails)
// {
//     auto req = MessageFactory::create<RpcRequest>();
//     Json::Value params;
//     params["a"] = 1;
//     req->setMethod("");
//     req->setParam(params);
//     EXPECT_FALSE(req->check());
// }

TEST(RpcRequestTest, MissingMethodCheckFails)
{
    auto req = MessageFactory::create<RpcRequest>();
    req->unserialize(make_json(KEY_METHOD, "", /*remove=*/true));
    EXPECT_FALSE(req->check());
}

TEST(RpcRequestTest, ParamsWrongTypeFails)
{
    auto req = MessageFactory::create<RpcRequest>();
    req->unserialize(make_json(KEY_PARAMS, "123"));
    EXPECT_FALSE(req->check());
}

TEST(RpcRequestTest, MissingParamsFails)
{
    auto req = MessageFactory::create<RpcRequest>();
    req->unserialize(make_json(KEY_PARAMS, "", /*remove=*/true));
    EXPECT_FALSE(req->check());
}

TEST(RpcRequestTest, SerializeUnserializeRoundTrip)
{
    auto req = MessageFactory::create<RpcRequest>();
    Json::Value params;
    params["x"] = 5;
    params["y"] = 6;
    req->setMethod("mul");
    req->setParam(params);

    auto req2 = MessageFactory::create<RpcRequest>();
    ASSERT_TRUE(req2->unserialize(req->serialize()));
    EXPECT_EQ(req2->method(), "mul");
    EXPECT_EQ(req2->param()["x"].asInt(), 5);
    EXPECT_EQ(req2->param()["y"].asInt(), 6);
}

// ==================== RpcResponse 测试 ====================

TEST(RpcResponseTest, InitialCheckFails)
{
    EXPECT_FALSE(MessageFactory::create<RpcResponse>()->check());
}

TEST(RpcResponseTest, ValidFieldsCheckPass)
{
    auto rsp = MessageFactory::create<RpcResponse>();
    Json::Value result;
    result["sum"] = 30;
    rsp->setRcode(RCode::RCODE_OK);
    rsp->setResult(result);
    EXPECT_EQ(rsp->rcode(), RCode::RCODE_OK);
    EXPECT_EQ(rsp->result(), result);
    EXPECT_TRUE(rsp->check());
}

TEST(RpcResponseTest, RcodeWrongTypeFails)
{
    auto rsp = MessageFactory::create<RpcResponse>();
    rsp->unserialize(make_json(KEY_RCODE, "\"ok\""));
    EXPECT_FALSE(rsp->check());
}

TEST(RpcResponseTest, MissingRcodeFails)
{
    auto rsp = MessageFactory::create<RpcResponse>();
    rsp->unserialize(make_json(KEY_RCODE, "", /*remove=*/true));
    EXPECT_FALSE(rsp->check());
}

TEST(RpcResponseTest, MissingResultFails)
{
    auto rsp = MessageFactory::create<RpcResponse>();
    rsp->unserialize(make_json(KEY_RESULT, "", /*remove=*/true));
    EXPECT_FALSE(rsp->check());
}

TEST(RpcResponseTest, ResultWrongTypeFails)
{
    auto rsp = MessageFactory::create<RpcResponse>();
    // result 被替换为数组，不是 object
    rsp->unserialize(make_json(KEY_RESULT, "[1,2,3]"));
    EXPECT_FALSE(rsp->check());
}

TEST(RpcResponseTest, SerializeUnserializeRoundTrip)
{
    auto rsp = MessageFactory::create<RpcResponse>();
    Json::Value result;
    result["val"] = 42;
    rsp->setRcode(RCode::RCODE_OK);
    rsp->setResult(result);

    auto rsp2 = MessageFactory::create<RpcResponse>();
    ASSERT_TRUE(rsp2->unserialize(rsp->serialize()));
    EXPECT_EQ(rsp2->rcode(), RCode::RCODE_OK);
    EXPECT_EQ(rsp2->result()["val"].asInt(), 42);
}

// ==================== TopicRequest 测试 ====================

TEST(TopicRequestTest, InitialCheckFails)
{
    EXPECT_FALSE(MessageFactory::create<TopicRequest>()->check());
}

TEST(TopicRequestTest, ValidPublishCheckPass)
{
    auto req = MessageFactory::create<TopicRequest>();
    req->setTopic("news");
    req->setTopicOptype(TopicOptype::TOPIC_PUBLISH);
    req->setTopicMsg("hello");
    EXPECT_EQ(req->topic(), "news");
    EXPECT_EQ(req->TopicOptype(), TopicOptype::TOPIC_PUBLISH);
    EXPECT_EQ(req->TopicMsg(), "hello");
    EXPECT_TRUE(req->check());
}

// check() 只验证字段存在且类型正确，不验证业务语义（空串由上层负责）
TEST(TopicRequestTest, AllOptypeCheckPass)
{
    using T = TopicOptype;
    for (auto op : {T::TOPIC_CREATE, T::TOPIC_REMOVE,
                    T::TOPIC_SUBSCRIBE, T::TOPIC_CANCEL, T::TOPIC_PUBLISH})
    {
        auto req = MessageFactory::create<TopicRequest>();
        req->setTopic("t");
        req->setTopicOptype(op);
        req->setTopicMsg("m");
        EXPECT_TRUE(req->check()) << "optype=" << static_cast<int>(op);
    }
}

TEST(TopicRequestTest, MissingTopicFails)
{
    auto req = MessageFactory::create<TopicRequest>();
    req->unserialize(make_json(KEY_TOPIC_KEY, "", /*remove=*/true));
    EXPECT_FALSE(req->check());
}

TEST(TopicRequestTest, TopicWrongTypeFails)
{
    auto req = MessageFactory::create<TopicRequest>();
    req->unserialize(make_json(KEY_TOPIC_KEY, "123"));
    EXPECT_FALSE(req->check());
}

TEST(TopicRequestTest, OptyreWrongTypeFails)
{
    auto req = MessageFactory::create<TopicRequest>();
    req->unserialize(make_json(KEY_OPTYPE, "\"publish\""));
    EXPECT_FALSE(req->check());
}

TEST(TopicRequestTest, MissingMsgFails)
{
    auto req = MessageFactory::create<TopicRequest>();
    req->unserialize(make_json(KEY_TOPIC_MSG, "", /*remove=*/true));
    EXPECT_FALSE(req->check());
}

TEST(TopicRequestTest, MsgWrongTypeFails)
{
    auto req = MessageFactory::create<TopicRequest>();
    req->unserialize(make_json(KEY_TOPIC_MSG, "456"));
    EXPECT_FALSE(req->check());
}

TEST(TopicRequestTest, SerializeUnserializeRoundTrip)
{
    auto req = MessageFactory::create<TopicRequest>();
    req->setTopic("sports");
    req->setTopicOptype(TopicOptype::TOPIC_SUBSCRIBE);
    req->setTopicMsg("subscribe_msg");

    auto req2 = MessageFactory::create<TopicRequest>();
    ASSERT_TRUE(req2->unserialize(req->serialize()));
    EXPECT_EQ(req2->topic(), "sports");
    EXPECT_EQ(req2->TopicOptype(), TopicOptype::TOPIC_SUBSCRIBE);
    EXPECT_EQ(req2->TopicMsg(), "subscribe_msg");
}

// ==================== TopicResponse 测试 ====================

TEST(TopicResponseTest, InitialCheckFails)
{
    EXPECT_FALSE(MessageFactory::create<TopicResponse>()->check());
}

TEST(TopicResponseTest, ValidRcodeCheckPass)
{
    auto rsp = MessageFactory::create<TopicResponse>();
    rsp->setRcode(RCode::RCODE_OK);
    EXPECT_EQ(rsp->rcode(), RCode::RCODE_OK);
    EXPECT_TRUE(rsp->check());
}

TEST(TopicResponseTest, ErrorRcodeCheckPass)
{
    // rcode 字段类型合法即可，错误码本身不影响 check
    auto rsp = MessageFactory::create<TopicResponse>();
    rsp->setRcode(RCode::RCODE_NOT_FOUND_TOPIC);
    EXPECT_TRUE(rsp->check());
}

TEST(TopicResponseTest, MissingRcodeFails)
{
    auto rsp = MessageFactory::create<TopicResponse>();
    rsp->unserialize(make_json(KEY_RCODE, "", /*remove=*/true));
    EXPECT_FALSE(rsp->check());
}

TEST(TopicResponseTest, SerializeUnserializeRoundTrip)
{
    auto rsp = MessageFactory::create<TopicResponse>();
    rsp->setRcode(RCode::RCODE_OK);

    auto rsp2 = MessageFactory::create<TopicResponse>();
    ASSERT_TRUE(rsp2->unserialize(rsp->serialize()));
    EXPECT_EQ(rsp2->rcode(), RCode::RCODE_OK);
}

// ==================== ServiceRequest 测试 ====================

TEST(ServiceRequestTest, InitialCheckFails)
{
    EXPECT_FALSE(MessageFactory::create<ServiceRequest>()->check());
}

TEST(ServiceRequestTest, RegistryWithHostCheckPass)
{
    auto req = MessageFactory::create<ServiceRequest>();
    req->setMethod("add");
    req->setServiceOptype(ServiceOptype::SERVICE_REGISTRY);
    req->setHost({"127.0.0.1", 8080});
    EXPECT_EQ(req->method(), "add");
    EXPECT_EQ(req->host().first, "127.0.0.1");
    EXPECT_EQ(req->host().second, 8080);
    EXPECT_TRUE(req->check());
}

TEST(ServiceRequestTest, DiscoveryWithoutHostCheckPass)
{
    auto req = MessageFactory::create<ServiceRequest>();
    req->setMethod("add");
    req->setServiceOptype(ServiceOptype::SERVICE_DISCOVERY);
    // discovery 不要求 host
    EXPECT_TRUE(req->check());
}

TEST(ServiceRequestTest, RegistryMissingHostFails)
{
    auto req = MessageFactory::create<ServiceRequest>();
    req->unserialize(make_json(KEY_HOST, "", /*remove=*/true));
    req->setMethod("add");
    req->setServiceOptype(ServiceOptype::SERVICE_REGISTRY);
    EXPECT_FALSE(req->check());
}

TEST(ServiceRequestTest, RegistryIncompleteHostFails)
{
    // host 存在但缺少 port 字段
    Json::Value incomplete_host;
    incomplete_host[KEY_HOST_IP] = "127.0.0.1";
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";

    auto req = MessageFactory::create<ServiceRequest>();
    req->unserialize(make_json(KEY_HOST, Json::writeString(wb, incomplete_host)));
    req->setMethod("add");
    req->setServiceOptype(ServiceOptype::SERVICE_REGISTRY);
    EXPECT_FALSE(req->check());
}

TEST(ServiceRequestTest, MissingMethodFails)
{
    auto req = MessageFactory::create<ServiceRequest>();
    req->unserialize(make_json(KEY_METHOD, "", /*remove=*/true));
    req->setServiceOptype(ServiceOptype::SERVICE_REGISTRY);
    req->setHost({"127.0.0.1", 8080});
    EXPECT_FALSE(req->check());
}

TEST(ServiceRequestTest, OptyreWrongTypeFails)
{
    auto req = MessageFactory::create<ServiceRequest>();
    req->unserialize(make_json(KEY_OPTYPE, "\"registry\""));
    EXPECT_FALSE(req->check());
}

TEST(ServiceRequestTest, SerializeUnserializeRoundTrip)
{
    auto req = MessageFactory::create<ServiceRequest>();
    req->setMethod("calc");
    req->setServiceOptype(ServiceOptype::SERVICE_REGISTRY);
    req->setHost({"10.0.0.1", 7070});

    auto req2 = MessageFactory::create<ServiceRequest>();
    ASSERT_TRUE(req2->unserialize(req->serialize()));
    EXPECT_EQ(req2->method(), "calc");
    EXPECT_EQ(req2->host().first, "10.0.0.1");
    EXPECT_EQ(req2->host().second, 7070);
}

// ==================== ServiceResponse 测试 ====================

TEST(ServiceResponseTest, InitialCheckFails)
{
    EXPECT_FALSE(MessageFactory::create<ServiceResponse>()->check());
}

TEST(ServiceResponseTest, DiscoveryResponseCheckPass)
{
    auto rsp = MessageFactory::create<ServiceResponse>();
    rsp->setRcode(RCode::RCODE_OK);
    rsp->setMethod("add");
    rsp->setServiceOptype(ServiceOptype::SERVICE_DISCOVERY);
    rsp->setHosts({{"127.0.0.1", 8080}, {"192.168.1.1", 9090}});

    EXPECT_EQ(rsp->rcode(), RCode::RCODE_OK);
    EXPECT_EQ(rsp->method(), "add");
    EXPECT_EQ(rsp->hosts().size(), 2u);
    EXPECT_EQ(rsp->hosts()[0].first,  "127.0.0.1");
    EXPECT_EQ(rsp->hosts()[0].second, 8080);
    EXPECT_EQ(rsp->hosts()[1].first,  "192.168.1.1");
    EXPECT_EQ(rsp->hosts()[1].second, 9090);
    EXPECT_TRUE(rsp->check());
}

TEST(ServiceResponseTest, RegistryResponseCheckPass)
{
    auto rsp = MessageFactory::create<ServiceResponse>();
    rsp->setRcode(RCode::RCODE_OK);
    rsp->setMethod("add");
    rsp->setServiceOptype(ServiceOptype::SERVICE_REGISTRY);
    rsp->setHosts({});  // 注册响应 hosts 为空 array，KEY_HOSTS 存在即可
    EXPECT_TRUE(rsp->check());
}

// setHosts({}) 写入空 array，KEY_HOSTS 存在且类型为 array，check 应通过
// TEST(ServiceResponseTest, EmptyHostsCheckPass)
// {
//     auto rsp = MessageFactory::create<ServiceResponse>();
//     rsp->setRcode(RCode::RCODE_OK);
//     rsp->setMethod("add");
//     rsp->setServiceOptype(ServiceOptype::SERVICE_DISCOVERY);
//     rsp->setHosts({});
//     EXPECT_EQ(rsp->hosts().size(), 0u);
//     EXPECT_TRUE(rsp->check());
// }

TEST(ServiceResponseTest, MissingRcodeFails)
{
    auto rsp = MessageFactory::create<ServiceResponse>();
    rsp->unserialize(make_json(KEY_RCODE, "", /*remove=*/true));
    EXPECT_FALSE(rsp->check());
}

TEST(ServiceResponseTest, MissingMethodFails)
{
    auto rsp = MessageFactory::create<ServiceResponse>();
    rsp->unserialize(make_json(KEY_METHOD, "", /*remove=*/true));
    EXPECT_FALSE(rsp->check());
}

// TEST(ServiceResponseTest, MissingHostsFails)
// {
//     auto rsp = MessageFactory::create<ServiceResponse>();
//     rsp->unserialize(make_json(KEY_HOST, "", /*remove=*/true));
//     EXPECT_FALSE(rsp->check());
// }

TEST(ServiceResponseTest, SerializeUnserializeRoundTrip)
{
    auto rsp = MessageFactory::create<ServiceResponse>();
    rsp->setRcode(RCode::RCODE_OK);
    rsp->setMethod("calc");
    rsp->setServiceOptype(ServiceOptype::SERVICE_DISCOVERY);
    rsp->setHosts({{"10.0.0.1", 7070}, {"10.0.0.2", 7071}});

    auto rsp2 = MessageFactory::create<ServiceResponse>();
    ASSERT_TRUE(rsp2->unserialize(rsp->serialize()));
    EXPECT_EQ(rsp2->rcode(), RCode::RCODE_OK);
    EXPECT_EQ(rsp2->method(), "calc");
    EXPECT_EQ(rsp2->hosts().size(), 2u);
    EXPECT_EQ(rsp2->hosts()[0].first,  "10.0.0.1");
    EXPECT_EQ(rsp2->hosts()[1].second, 7071);
}

// ==================== 公共序列化/反序列化健壮性测试 ====================

TEST(SerializeTest, InvalidJsonUnserializeFails)
{
    std::vector<std::shared_ptr<BaseMessage>> msgs = {
        MessageFactory::create<RpcRequest>(),
        MessageFactory::create<RpcResponse>(),
        MessageFactory::create<TopicRequest>(),
        MessageFactory::create<TopicResponse>(),
        MessageFactory::create<ServiceRequest>(),
        MessageFactory::create<ServiceResponse>(),
    };
    for (auto &msg : msgs) {
        EXPECT_FALSE(msg->unserialize("not_json{{{"));
        EXPECT_FALSE(msg->unserialize(""));
    }
}

TEST(SerializeTest, SerializeTwiceIsSame)
{
    auto req = MessageFactory::create<RpcRequest>();
    Json::Value p;
    p["k"] = 1;
    req->setMethod("foo");
    req->setParam(p);
    EXPECT_EQ(req->serialize(), req->serialize());
}

// ==================== 主函数 ====================

int main(int argc, char **argv)
{
    Logger::instance().init(true, "log/log.log", spdlog::level::info);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}