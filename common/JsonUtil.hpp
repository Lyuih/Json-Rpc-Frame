#pragma once
#include <iostream>
#include <memory>
#include <sstream>
#include <json/json.h>
class JsonUtil
{
public:
    /**
     * @description,序列化，输入输出型参数
     * @param:
     * root:表示要序列化的json对象
     * str:表示序列化之后的字符串
     */
    static bool serialize(const Json::Value &root, std::string &str)
    {
        // 1.创建序列化对象
        Json::StreamWriterBuilder swb;
        std::unique_ptr<Json::StreamWriter> sw(swb.newStreamWriter());
        // 2.序列化
        std::stringstream ss;
        int ret = sw->write(root, &ss);
        if (ret < 0)
        {
            std::cout << "序列化失败" << std::endl;
            return false;
        }
        str = ss.str();
        return true;
    }
    /**
     * @description:序列化，输入输出型参数
     * @param:
     * str:表示需要反序列化的字符串
     * root:表示要反序列化之后的json对象
     */
    static bool unserialize(const std::string &str, Json::Value &root)
    {
        Json::CharReaderBuilder crb;
        std::unique_ptr<Json::CharReader> cr(crb.newCharReader());

        bool ret = cr->parse(str.c_str(), str.c_str() + str.size(), &root, nullptr);
        if (!ret)
        {
            std::cout << "反序列化失败" << std::endl;
            return false;
        }
        return true;
    }
};