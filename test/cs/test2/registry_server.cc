#include "../../../server/server.hpp"
#include "../../../common/fields.hpp"
#include "../../../client/client.hpp"
#include "../../../server/registry.hpp"

int main()
{
    Logger::instance().init(true, "log_r/log.log", spdlog::level::debug);
    Lyuih::server::RegistryServer reg_server(8080);
    reg_server.start();
    return 0;
}