// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "launcher_files.h"

#include <array>
#include <atomic>
#include <chrono>
#include <system_error>

namespace UVE::LauncherUI {
namespace {

inline constexpr std::uint64_t kBytesPerUnitStep = 1024U;
inline constexpr std::array<std::string_view, 5U> kByteUnits{"B", "KB", "MB", "GB", "TB"};

} // namespace

std::string FormatByteSize(const std::uint64_t bytes) {
    std::uint64_t value = bytes;
    std::size_t unit = 0U;
    while (value >= kBytesPerUnitStep && unit + 1U < kByteUnits.size()) {
        value /= kBytesPerUnitStep;
        ++unit;
    }
    return std::to_string(value) + " " + std::string(kByteUnits[unit]);
}

std::filesystem::path UniqueTemporaryPath(const std::string_view prefix, const std::string_view suffix) {
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    if (error) {
        return {};
    }
    // The clock separates runs of the program; the counter separates calls within one run.
    static std::atomic<std::uint64_t> counter{0U};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return root / (std::string(prefix) + std::to_string(stamp) + "-" + std::to_string(counter.fetch_add(1U)) +
                   std::string(suffix));
}

} // namespace UVE::LauncherUI
