// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "logging_macros.h"

#include <cstdio>
#include <fstream>
#include <mutex>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace UVE::Logging {
namespace {

std::mutex g_logMutex;
std::ofstream g_logFile;

[[nodiscard]] const char* LevelTag(const LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    }
    return "INFO";
}

} // namespace

void SetLogFile(const std::filesystem::path& path) {
    std::lock_guard lock(g_logMutex);
    g_logFile.close();
    if (path.empty()) {
        return;
    }
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    g_logFile.open(path, std::ios::out | std::ios::trunc);
}

void CloseLogFile() {
    std::lock_guard lock(g_logMutex);
    g_logFile.close();
}

void WriteLogLine(const LogLevel level, const std::string_view message) {
    std::lock_guard lock(g_logMutex);
    const char* const tag = LevelTag(level);
#if defined(__ANDROID__)
    int prio = ANDROID_LOG_INFO;
    if (level == LogLevel::Warning) prio = ANDROID_LOG_WARN;
    else if (level == LogLevel::Error) prio = ANDROID_LOG_ERROR;
    else if (level == LogLevel::Fatal) prio = ANDROID_LOG_FATAL;
    __android_log_print(prio, "UniVex", "%.*s", static_cast<int>(message.size()), message.data());
#else
    if (level == LogLevel::Error || level == LogLevel::Fatal) {
        std::fprintf(stderr, "[%s] %.*s\n", tag, static_cast<int>(message.size()), message.data());
    }
#endif
    if (g_logFile.is_open()) {
        g_logFile << '[' << tag << "] " << message << '\n';
        g_logFile.flush();
    }
}

} // namespace UVE::Logging
