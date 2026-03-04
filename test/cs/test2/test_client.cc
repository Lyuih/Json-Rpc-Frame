#include "../../../common/fields.hpp"
#include "../../../client/client.hpp"

void callback(const Json::Value &result)
{
    LOG_DEBUG("callback result: %d", result.asInt());
}

int main()
{
    Logger::instance().init(true, "log_c/log.log", spdlog::level::debug);

    Lyuih::client::RpcClient client(true, "127.0.0.1", 8080);

    Json::Value params, result;
    params["num1"] = 11;
    params["num2"] = 22;
    bool ret = client.call("Add", params, result);
    if (ret != false)
    {
        LOG_DEBUG("result: %d", result.asInt());
    }

    Lyuih::client::RpcCaller::JsonAsyncResponse res_future;
    params["num1"] = 33;
    params["num2"] = 44;
    ret = client.call("Add", params, res_future);
    if (ret != false)
    {
        result = res_future.get();
        LOG_DEBUG("result: %d", result.asInt());
    }

    params["num1"] = 55;
    params["num2"] = 66;
    ret = client.call("Add", params, callback);
    LOG_DEBUG("-------");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}