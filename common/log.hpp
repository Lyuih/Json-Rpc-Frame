#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/async.h>
#include <memory>
#include <mutex>

class Logger
{
public:
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    static Logger &instance()
    {
        static Logger logger_instance;
        return logger_instance;
    }

    // 获取 logger 对象
    std::shared_ptr<spdlog::logger> get_logger() const
    {
        return logger_;
    }

    void init(bool mode, const std::string file_name, spdlog::level::level_enum level)
    {
        std::call_once(init_flag_, [&]()
                       {
            // 1. 初始化异步日志所需的线程池
            spdlog::init_thread_pool(8192, 1);

            if (mode == false) { // Debug mode
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink->set_level(spdlog::level::trace);
                logger_ = std::make_shared<spdlog::async_logger>(
                    "console_logger", 
                    console_sink, 
                    spdlog::thread_pool(), 
                    spdlog::async_overflow_policy::block
                );
                logger_->set_level(spdlog::level::trace);
            } else { // Release mode
                auto daily_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(file_name, 0, 0);
                daily_sink->set_level(level);
                logger_ = std::make_shared<spdlog::async_logger>(
                    "file_logger", 
                    daily_sink, 
                    spdlog::thread_pool(), 
                    spdlog::async_overflow_policy::block
                );
                logger_->set_level(level);
                logger_->flush_on(spdlog::level::warn);
            }

            // 2. 设置统一的日志格式，包含源文件和行号 (%@)
            logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
            
            // 3. 注册 logger，以便全局访问
            spdlog::register_logger(logger_); });
    }

private:
    // 在 init 调用前，提供一个默认的、安全的、什么都不做的 logger
    Logger()
    {
        logger_ = spdlog::stdout_color_mt("default_safe_logger");
        logger_->set_level(spdlog::level::off); // 默认关闭，init后才生效
    }
    // 析构函数中关闭 spdlog，刷新所有日志
    ~Logger()
    {
        spdlog::shutdown();
    }
    std::shared_ptr<spdlog::logger> logger_;
    std::once_flag init_flag_;
};

// 简化宏，将文件和行号信息交给 spdlog 的 pattern 来处理
// 使用 SPDLOG_FMT_ENABLED 来获得编译期格式化字符串检查
#define LOG_TRACE(format, ...) Logger::instance().get_logger()->trace("[{}:{}]: " format,__FILE__,__LINE__,##__VA_ARGS__)
#define LOG_DEBUG(format, ...) Logger::instance().get_logger()->debug("[{}:{}]: " format,__FILE__,__LINE__,##__VA_ARGS__)
#define LOG_INFO(format, ...) Logger::instance().get_logger()->info("[{}:{}]: " format,__FILE__,__LINE__,##__VA_ARGS__)
#define LOG_WARN(format, ...) Logger::instance().get_logger()->warn("[{}:{}]: " format,__FILE__,__LINE__,##__VA_ARGS__)
#define LOG_ERROR(format, ...) Logger::instance().get_logger()->error("[{}:{}]: " format,__FILE__,__LINE__,##__VA_ARGS__)
#define LOG_FATAL(format, ...) Logger::instance().get_logger()->critical("[{}:{}]: " format,__FILE__,__LINE__,##__VA_ARGS__)