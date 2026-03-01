#pragma once
#include <sstream>
#include <random>
#include <string>
#include <iomanip>

std::string generate_uuid_v4()
{
    // 1.初始化高质量随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    // 生成0-15的随机数
    std::uniform_int_distribution<> hex_dist(0, 15);
    // 生成8-11的随机数,作为变体位
    std::uniform_int_distribution<> variant_dist(8, 11);
    // 2.定义UUID字符缓冲区
    std::ostringstream oss;
    oss << std::hex << std::nouppercase; // 输出小写十六进制
    // 3.分段生成
    // 1
    for (int i = 0; i < 8; ++i)
    {
        oss << hex_dist(gen);
    }
    oss << "-";
    // 2
    for (int i = 0; i < 4; ++i)
    {
        oss << hex_dist(gen);
    }
    oss << "-";
    // 3
    oss << 4; // 版本位
    for (int i = 0; i < 3; ++i)
    {
        oss << hex_dist(gen);
    }
    oss << "-";
    // 4
    oss << variant_dist(gen);
    for(int i = 0;i<3;++i)
    {
        oss<<hex_dist(gen);
    }
    oss << "-";

    //5
    for(int i = 0;i<12;++i)
    {
        oss<<hex_dist(gen);
    }
    return oss.str();
}