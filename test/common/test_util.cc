#include <iostream>
#include "../../common/log.hpp"
#include "../../common/JsonUtil.hpp"
#include "../../common/uuidUtil.hpp"

void test_logger(bool flag)
{
    Logger::instance().init(flag, "log/log.log", spdlog::level::info);
    LOG_DEBUG("{}", "test");
    LOG_DEBUG("{}", 1);
    LOG_DEBUG("{}", 1.1);
    LOG_FATAL("{}", "致命错误");
}

void Json_serialize_test()
{
    Json::Value root;
    root["name"] = "lyuih";
    root["age"] = 21;
    root["socre"].append(100);
    root["socre"].append(100);
    root["socre"].append(100);
    Json::Value hobby;
    hobby["sport"] = "running";
    root["hobby"] = hobby;
    std::string s;
    JsonUtil::serialize(root, s);
    std::cout << "序列化结果:\n"
              << s << std::endl;
    // 反序列化
    Json::Value stu;
    JsonUtil::unserialize(s, stu);
    std::cout << "反序列化结果:\n"
              << "name:" << stu["name"].asString() << '\n'
              << "age:" << stu["age"].asInt() << '\n'
              << "socre:" << stu["socre"][0].asFloat() << ' ' << stu["socre"][1].asFloat() << ' ' << stu["socre"][0].asFloat() << '\n'
              << "hobby:" << stu["hobby"]["sport"].asString() << '\n';
}

void uuid_test()
{
    std::cout << generate_uuid_v4() << std::endl;
    std::cout << generate_uuid_v4() << std::endl;
    std::cout << generate_uuid_v4() << std::endl;
}

int main()
{
    // 1.测试日志功能,false debug模式，true release模式
    //  test_logger(false);
    //  test_logger(true);
    // 2.json序列化/反序列化测试
    // Json_serialize_test();
    // 3.uuid生成测试
    uuid_test();
    return 0;
}