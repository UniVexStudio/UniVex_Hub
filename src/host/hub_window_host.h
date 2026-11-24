// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace UVE::Window {
class AndroidWindowManager;
}

namespace UVE::Host {

struct HubWindowConfig final {
    std::string title;
    std::uint32_t width = 1200U;
    std::uint32_t height = 760U;
    bool borderless = true;
    std::uint32_t focusedFrameRateCap = 60U;
    std::uint32_t unfocusedFrameRateCap = 10U;
    std::filesystem::path logFile;
    void* nativeSurface = nullptr;
};

/// Minimal standalone window and OpenGL / OpenGL ES 3 surface host for UniVex Hub and Setup.
/// Desktop builds create a borderless GLFW OpenGL Core context; Android builds wrap
/// AndroidWindowManager over the NativeActivity ANativeWindow surface.
class HubWindowHost final {
public:
    static constexpr int kUnhandledExceptionExitCode = 1;

    explicit HubWindowHost(HubWindowConfig config);
    ~HubWindowHost();
    HubWindowHost(const HubWindowHost&) = delete;
    HubWindowHost& operator=(const HubWindowHost&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown();

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] bool IsCloseRequested() const noexcept;
    [[nodiscard]] void* GetNativeWindowHandle() const noexcept;
    [[nodiscard]] Window::AndroidWindowManager* GetAndroidWindowManager() noexcept;

    void BeginFrame();
    void EndFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Host
