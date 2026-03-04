
#include "../../../common/dispatcher.hpp"
#include "../../../common/net.hpp"
#include "../../../server/router.hpp"
#include "../../../common/message.hpp"


// void onRpcRequest(const Lyuih::BaseConnection::ptr& conn,Lyuih::RpcRequest::ptr& msg){
//     LOG(Level::DEBUG,"接收到rpc请求");
//     std::string str = msg->serialize();
//     std::cout<<str<<std::endl;

//     Lyuih::RpcResponse::ptr rrp = Lyuih::MessageFactory::create<Lyuih::RpcResponse>();
//     rrp->setId(Lyuih::UUID::uuid());
//     rrp->setMType(Lyuih::MType::RSP_RPC);
//     rrp->setRCode(Lyuih::RCode::RCODE_OK);
//     rrp->setResult(33);
//     conn->send(rrp);
// }


// void onTopicRequest(const Lyuih::BaseConnection::ptr& conn,Lyuih::TopicRequest::ptr& msg){
//     LOG(Level::DEBUG,"接收到Topic请求");
//     std::string str = msg->serialize();
//     std::cout<<str<<std::endl;

//     Lyuih::TopicResponse::ptr rrp = Lyuih::MessageFactory::create<Lyuih::TopicResponse>();
//     rrp->setId(Lyuih::UUID::uuid());
//     rrp->setMType(Lyuih::MType::RSP_TOPIC);
//     rrp->setRCode(Lyuih::RCode::RCODE_OK);
//     // rrp->setResult(44);
//     conn->send(rrp);
// }

void add(const Json::Value& req,Json::Value& rsp){
    int num1 = req["num1"].asInt();
    int num2 = req["num2"].asInt();
    rsp = num1+num2;
}



int main()
{

    Logger::instance().init(true, "log_s/log.log", spdlog::level::info);


    auto dispatcher = std::make_shared<Lyuih::Dispatcher>();
    auto router = std::make_shared<Lyuih::server::RpcRouter>();
    std::unique_ptr<Lyuih::server::SDescribeFactory> desc_factory(new Lyuih::server::SDescribeFactory());
    desc_factory->setMethodName("Add");
    desc_factory->PushParam("num1",Lyuih::server::VType::INTEGRAL);
    desc_factory->PushParam("num2",Lyuih::server::VType::INTEGRAL);
    desc_factory->setRetType(Lyuih::server::VType::INTEGRAL);
    desc_factory->setCallback(add);
    router->resisterMethod(desc_factory->build());

    auto cb = std::bind(&Lyuih::server::RpcRouter::onRpcRequest,router.get(),std::placeholders::_1,std::placeholders::_2);

    dispatcher->registerHandler<Lyuih::RpcRequest>(Lyuih::MType::REQ_RPC,cb);
    // dispatcher->registerHandler<Lyuih::TopicRequest>(Lyuih::MType::REQ_TOPIC,onTopicRequest);
    auto message_cb = std::bind(&Lyuih::Dispatcher::onMesssage,dispatcher.get(),std::placeholders::_1,std::placeholders::_2);

    Lyuih::BaseServer::ptr server = Lyuih::ServerFactory::create(8080);
    server->setMessageCallback(message_cb);
    server->start();
    return 0;
}