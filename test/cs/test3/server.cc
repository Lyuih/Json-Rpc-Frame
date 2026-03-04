#include "../../../server/server.hpp"

int main()
{
    Logger::instance().init(true, "log_s/log.log", spdlog::level::debug);

    auto server = std::make_shared<Lyuih::server::TopicServer>(7070);
    server->start();
    return 0;
}