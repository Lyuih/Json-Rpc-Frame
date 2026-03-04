#include "../../../client/requestor.hpp"
#include "../../../client/caller.hpp"
#include "../../../common/dispatcher.hpp"
#include "../../../common/net.hpp"
#include <thread>

// void onMessage(const Lyuih::BaseConnection::ptr& conn,Lyuih::BaseMessage::ptr& msg){
//     std::string str = msg->serialize();
//     std::cout<<str<<std::endl;

// }
// void onRpcResponse(const Lyuih::BaseConnection::ptr &conn, Lyuih::RpcResponse::ptr &msg)
// {

//     LOG(Level::DEBUG, "接收到rpc响应");
//     std::string str = msg->serialize();
//     std::cout << str << std::endl;
// }

// void onTopicResponse(const Lyuih::BaseConnection::ptr &conn, Lyuih::TopicResponse::ptr &msg)
// {
//     LOG(Level::DEBUG, "接收到Topic响应");
//     std::string str = msg->serialize();
//     std::cout << str << std::endl;
// }

void callback(const Json::Value &result)
{
    // LOG(Level::DEBUG, "callback result：%d", result.asInt());
    LOG_DEBUG("callback result:{}", result.asInt());
}

int main()
{
    Logger::instance().init(true, "log_c/log.log", spdlog::level::info);
    auto requestor = std::make_shared<Lyuih::client::Requestor>();
    auto caller = std::make_shared<Lyuih::client::RpcCaller>(requestor);

    auto dispatcher = std::make_shared<Lyuih::Dispatcher>();

    auto rsp_cb = std::bind(&Lyuih::client::Requestor::onResponse, requestor.get(),
                            std::placeholders::_1, std::placeholders::_2);

    dispatcher->registerHandler<Lyuih::BaseMessage>(Lyuih::MType::RSP_RPC, rsp_cb);
    // dispatcher->registerHandler<Lyuih::TopicResponse>(Lyuih::MType::RSP_TOPIC, onTopicResponse);

    auto message_cb = std::bind(&Lyuih::Dispatcher::onMesssage, dispatcher.get(), std::placeholders::_1, std::placeholders::_2);

    Lyuih::BaseClient::ptr client = Lyuih::ClientFactory::create("127.0.0.1", 8080);
    client->setMessageCallback(message_cb);
    client->connect();

    auto conn = client->connection();
    Json::Value params, result;
    params["num1"] = 11;
    params["num2"] = 22;
    bool ret = caller->call(conn, "Add", params, result);
    if (ret != false)
    {
        LOG_DEBUG( "result:{}", result.asInt());
    }
    // Lyuih::RpcRequest::ptr rrp = Lyuih::MessageFactory::create<Lyuih::RpcRequest>();
    // rrp->setId(Lyuih::UUID::uuid());
    // rrp->setMType(Lyuih::MType::REQ_RPC);
    // rrp->setMethod("add");
    // Json::Value param;
    // param["num1"] = 22;
    // param["num2"] = 33;
    // rrp->setParams(param);

    // client->send(rrp);

    // Lyuih::TopicRequest::ptr trp = Lyuih::MessageFactory::create<Lyuih::TopicRequest>();
    // trp->setId(Lyuih::UUID::uuid());
    // trp->setMType(Lyuih::MType::REQ_TOPIC);
    // // trp->setMethod("music");
    // trp->setOptype(Lyuih::TopicOptype::TOPIC_CREATE);
    // trp->setTopicKey("music");
    // // trp->setTopicMsg("hello world");
    // client->send(trp);

    Lyuih::client::RpcCaller::JsonAsyncResponse res_future;
    params["num1"] = 33;
    params["num2"] = 44;
    ret = caller->call(conn, "Add", params, res_future);
    if (ret != false)
    {
        result = res_future.get();
        LOG_DEBUG("result:{}",result.asInt());
    }

    params["num1"] = 44;
    params["num2"] = 55;
    ret = caller->call(conn,"Add",params,callback);
    std::this_thread::sleep_for(std::chrono::seconds(10));
    client->shutdown();
    return 0;
}