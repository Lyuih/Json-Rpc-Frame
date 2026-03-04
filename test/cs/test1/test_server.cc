#include "../../../common/fields.hpp"
#include "../../../server/server.hpp"

void Add(const Json::Value &req, Json::Value &rsp) {
    int num1 = req["num1"].asInt();
    int num2 = req["num2"].asInt();
    rsp = num1 + num2;
}
int main()
{
    Logger::instance().init(true, "log_s/log.log", spdlog::level::debug);
    std::unique_ptr<Lyuih::server::SDescribeFactory> desc_factory(new Lyuih::server::SDescribeFactory());
    desc_factory->setMethodName("Add");
    desc_factory->PushParam("num1", Lyuih::server::VType::INTEGRAL);
    desc_factory->PushParam("num2", Lyuih::server::VType::INTEGRAL);
    desc_factory->setRetType(Lyuih::server::VType::INTEGRAL);
    desc_factory->setCallback(Add);
    
    Lyuih::server::RpcServer server(Lyuih::Address("127.0.0.1", 9090));
    server.registerMethod(desc_factory->build());
    server.start();
    return 0;
}