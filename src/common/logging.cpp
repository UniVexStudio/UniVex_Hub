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

[[nodiscard]] const char* LevelTagUVE(const LogLevelUVE level) noexcept {
    switch (level) {
    case LogLevelUVE::Info: return "INFO";
    case LogLevelUVE::Warning: return "WARN";
    case LogLevelUVE::Error: return "ERROR";
    case LogLevelUVE::Fatal: return "FATAL";
    }
    return "INFO";
}

} // namespace

void SetLogFileUVE(const std::filesystem::path& path) {
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

void CloseLogFileUVE() {
    std::lock_guard lock(g_logMutex);
    g_logFile.close();
}

void WriteLogLineUVE(const LogLevelUVE level, const std::string_view message) {
    std::lock_guard lock(g_logMutex);
    const char* const tag = LevelTagUVE(level);
#if defined(__ANDROID__)
    int prio = ANDROID_LOG_INFO;
    if (level == LogLevelUVE::Warning) prio = ANDROID_LOG_WARN;
    else if (level == LogLevelUVE::Error) prio = ANDROID_LOG_ERROR;
    else if (level == LogLevelUVE::Fatal) prio = ANDROID_LOG_FATAL;
    __android_log_print(prio, "UniVex", "%.*s", static_cast<int>(message.size()), message.data());
#else
    if (level == LogLevelUVE::Error || level == LogLevelUVE::Fatal) {
        std::fprintf(stderr, "[%s] %.*s\n", tag, static_cast<int>(message.size()), message.data());
    }
#endif
    if (g_logFile.is_open()) {
        g_logFile << '[' << tag << "] " << message << '\n';
        g_logFile.flush();
    }
}

} // namespace UVE::Logging
