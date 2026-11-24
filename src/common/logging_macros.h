// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace UVE::Logging {

enum class LogLevel { Info, Warning, Error, Fatal };

void SetLogFile(const std::filesystem::path& path);
void CloseLogFile();
void WriteLogLine(LogLevel level, std::string_view message);

template <typename... Args>
void LogFormat(const LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
    WriteLogLine(level, std::format(fmt, std::forward<Args>(args)...));
}

inline void LogFormat(const LogLevel level, const std::string_view message) {
    WriteLogLine(level, message);
}

} // namespace UVE::Logging

#define UVE_INFO(...) ::UVE::Logging::LogFormat(::UVE::Logging::LogLevel::Info, __VA_ARGS__)
#define UVE_WARNING(...) ::UVE::Logging::LogFormat(::UVE::Logging::LogLevel::Warning, __VA_ARGS__)
#define UVE_ERROR(...) ::UVE::Logging::LogFormat(::UVE::Logging::LogLevel::Error, __VA_ARGS__)
#define UVE_FATAL(...) ::UVE::Logging::LogFormat(::UVE::Logging::LogLevel::Fatal, __VA_ARGS__)
