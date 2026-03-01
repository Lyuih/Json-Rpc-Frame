#include "../common/log.hpp"



int main() {
    // 初始化日志系统
    // 在发布模式下，记录 INFO 及以上级别的日志到 my_log.txt
    Logger::instance().init(false, "my_log.txt", spdlog::level::info);

    LOG_TRACE("This trace message will not be shown.");
    LOG_INFO("Hello, {}!", "world");
    LOG_WARN("This is a warning.");
    LOG_ERROR("This is an error with a number: {}.", 123);

    // 程序结束时，Logger 的析构函数会自动调用 spdlog::shutdown()
    return 0;
}