#pragma once
//
// Log.h
// 极简日志工具：LOG_INFO / LOG_WARN / LOG_ERROR
// - 输出到 stdout（INFO）/ stderr（WARN/ERROR）
// - 带时间、级别前缀
// - 不引入 spdlog 等重依赖
//

#include <sstream>
#include <string>

namespace routes_label::utils {

enum class LogLevel { Info, Warn, Error };

// 实际写出函数（由 Log.cpp 实现）
void log_write(LogLevel level, const std::string& msg);

}  // namespace routes_label::utils

// 用法：LOG_INFO("foo " << 42);
#define ROUTES_LOG(level, expr)                                                  \
    do {                                                                         \
        std::ostringstream _oss;                                                 \
        _oss << expr;                                                            \
        ::routes_label::utils::log_write(level, _oss.str());                     \
    } while (0)

#define LOG_INFO(expr)  ROUTES_LOG(::routes_label::utils::LogLevel::Info,  expr)
#define LOG_WARN(expr)  ROUTES_LOG(::routes_label::utils::LogLevel::Warn,  expr)
#define LOG_ERROR(expr) ROUTES_LOG(::routes_label::utils::LogLevel::Error, expr)
