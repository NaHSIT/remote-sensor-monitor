// 模块：公共支撑；作用：提供统一的日志记录接口；编写内容：补充日志级别、输出目标、格式和线程安全处理。

// 模块：日志工具
// 作用：封装统一日志接口，后续无缝替换为 spdlog
// 使用方式：LOG_INFO("Server started on port %d", 9000);

#ifndef LOGGER_H
#define LOGGER_H

#include <cstdio>

// ============================================================================
// 日志级别定义
// ============================================================================

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5
};

// ============================================================================
// 简易日志实现（当前阶段）
// TODO: 替换为 spdlog，只需修改此文件内部实现，其他文件不动
// ============================================================================

namespace common {

class Logger {
public:
    static void init(LogLevel level = LogLevel::Info) {
        currentLevel_ = level;
    }

    static void setLevel(LogLevel level) { currentLevel_ = level; }
    static LogLevel getLevel() { return currentLevel_; }

    template<typename... Args>
    static void log(LogLevel level, const char* tag, const char* fmt, Args&&... args) {
        if (level < currentLevel_) return;
        printf("[%s] ", levelStr(level));
        if (tag) printf("[%s] ", tag);
        printf(fmt, std::forward<Args>(args)...);
        printf("\n");
    }

private:
    static LogLevel currentLevel_;

    static const char* levelStr(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            default:              return "?????";
        }
    }
};

} // namespace common

// ============================================================================
// 便捷宏 —— 全项目统一使用
// ============================================================================
// 使用示例：
//   LOG_INFO("NET", "Connection established from %s", addr.c_str());
//   LOG_ERROR("BIZ", "Invalid data from device %d", deviceId);
//   LOG_DEBUG("UI",  "Curve refreshed at %d FPS", fps);

#define LOG_TRACE(tag, fmt, ...) common::Logger::log(LogLevel::Trace, tag, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...) common::Logger::log(LogLevel::Debug, tag, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  common::Logger::log(LogLevel::Info,  tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  common::Logger::log(LogLevel::Warn,  tag, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) common::Logger::log(LogLevel::Error, tag, fmt, ##__VA_ARGS__)
#define LOG_FATAL(tag, fmt, ...) common::Logger::log(LogLevel::Fatal, tag, fmt, ##__VA_ARGS__)

#endif // LOGGER_H
