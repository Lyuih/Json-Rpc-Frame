#include "../../../client/client.hpp"

int main()
{
    Logger::instance().init(true, "log_p/log.log", spdlog::level::debug);
    //1. 实例化客户端对象
    auto client = std::make_shared<Lyuih::client::TopicClient>("127.0.0.1", 7070);
    //2. 创建主题
    bool ret = client->create("hello");
    if (ret == false) {
        LOG_DEBUG("创建主题失败！");
    }
    //3. 向主题发布消息
    for (int i = 0; i < 10; i++) {
        client->publish("hello", "Hello World-" + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    client->shutdown();
    return 0;
}