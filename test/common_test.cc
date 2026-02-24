#include <iostream>
#include "../common/log.hpp"

void test_logger(bool flag)
{
    Logger::instance().init(flag,"log/log.log",spdlog::level::info);
    LOG_DEBUG("{}","test");
    LOG_DEBUG("{}",1);
    LOG_DEBUG("{}",1.1);
}

int main()
{
    //1.测试日志功能,false debug模式，true release模式
    test_logger(false);
    // test_logger(true);
    return 0;
}