// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace UVE::Logging {

enum class LogLevelUVE { Info, Warning, Error, Fatal };

void SetLogFileUVE(const std::filesystem::path& path);
void CloseLogFileUVE();
void WriteLogLineUVE(LogLevelUVE level, std::string_view message);

template <typename... Args>
void LogFormatUVE(const LogLevelUVE level, std::format_string<Args...> fmt, Args&&... args) {
    WriteLogLineUVE(level, std::format(fmt, std::forward<Args>(args)...));
}

inline void LogFormatUVE(const LogLevelUVE level, const std::string_view message) {
    WriteLogLineUVE(level, message);
}

} // namespace UVE::Logging

#define UVE_INFO(...) ::UVE::Logging::LogFormatUVE(::UVE::Logging::LogLevelUVE::Info, __VA_ARGS__)
#define UVE_WARNING(...) ::UVE::Logging::LogFormatUVE(::UVE::Logging::LogLevelUVE::Warning, __VA_ARGS__)
#define UVE_ERROR(...) ::UVE::Logging::LogFormatUVE(::UVE::Logging::LogLevelUVE::Error, __VA_ARGS__)
#define UVE_FATAL(...) ::UVE::Logging::LogFormatUVE(::UVE::Logging::LogLevelUVE::Fatal, __VA_ARGS__)
