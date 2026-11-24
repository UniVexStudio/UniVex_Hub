// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace UVE::Window {
class AndroidWindowManagerUVE;
}

namespace UVE::Host {

struct HubWindowConfigUVE final {
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
/// AndroidWindowManagerUVE over the NativeActivity ANativeWindow surface.
class HubWindowHostUVE final {
public:
    static constexpr int kUnhandledExceptionExitCodeUVE = 1;

    explicit HubWindowHostUVE(HubWindowConfigUVE config);
    ~HubWindowHostUVE();
    HubWindowHostUVE(const HubWindowHostUVE&) = delete;
    HubWindowHostUVE& operator=(const HubWindowHostUVE&) = delete;

    [[nodiscard]] bool InitializeUVE();
    void ShutdownUVE();

    [[nodiscard]] bool IsRunningUVE() const noexcept;
    [[nodiscard]] bool IsCloseRequestedUVE() const noexcept;
    [[nodiscard]] void* GetNativeWindowHandleUVE() const noexcept;
    [[nodiscard]] Window::AndroidWindowManagerUVE* GetAndroidWindowManagerUVE() noexcept;

    void BeginFrameUVE();
    void EndFrameUVE();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Host
