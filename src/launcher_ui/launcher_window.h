// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "prism/Widget.h"

namespace UVE::Host {
class HubWindowHostUVE;
}

namespace UVE::LauncherUI {

/// PRISM's fonts, themes and locales ship beside every launcher executable under this name.
inline constexpr std::string_view kPrismAssetsDirectoryNameUVE = "prism_assets";
/// Process exit code when the window or its interface cannot start.
inline constexpr int kLauncherStartupFailedExitCodeUVE = 1;

struct LauncherWindowDescUVE final {
    /// The window's name for the operating system (taskbar, window switcher).
    std::string title;
    /// Whether the title bar repeats the mark and title.
    bool showTitleInChrome = false;
    /// Whether to draw the title bar and window buttons at all (off on full-screen Android).
    bool windowChrome = true;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    /// PRISM's fonts, themes and locales (normally kPrismAssetsDirectoryNameUVE beside the executable).
    std::filesystem::path prismAssetsDirectory;
    std::filesystem::path logFile;
    std::vector<std::string> commandLine;
    /// Android: the activity's surface to draw into (desktop builds open a window of their own).
    void* nativeSurface = nullptr;
    /// Runs before every frame with the running host; returning false ends the loop.
    std::function<bool(Host::HubWindowHostUVE&)> beforeFrame;
    /// Edges of the surface the interface keeps clear (display cutouts / IME), in logical pixels.
    std::function<prism::Margin()> safeArea;
    /// Automation: stop after this many frames, then save the window to `screenshot` if set.
    std::optional<int> frameLimit;
    std::filesystem::path screenshot;
};

[[nodiscard]] int RunLauncherWindowUVE(const LauncherWindowDescUVE& desc,
                                       const std::function<prism::Ref<prism::Widget>()>& buildRoot,
                                       const std::function<void()>& tick);

void RequestLauncherCloseUVE();

} // namespace UVE::LauncherUI
