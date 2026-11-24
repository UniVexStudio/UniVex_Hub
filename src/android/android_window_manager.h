// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>

namespace UVE::Window {

/// Standalone Android EGL / OpenGL ES 3 surface manager for UniVex Hub.
/// Handles fallback EGL config selection, 1x1 pbuffer idle surface during detach,
/// surface reattachment on APP_CMD_INIT_WINDOW, and VSync swap interval.
class AndroidWindowManager final {
public:
    AndroidWindowManager(void* nativeWindow, bool vsync = true);
    ~AndroidWindowManager();
    AndroidWindowManager(const AndroidWindowManager&) = delete;
    AndroidWindowManager& operator=(const AndroidWindowManager&) = delete;

    [[nodiscard]] bool IsValid() const noexcept;
    void SetFocused(bool focused) noexcept;
    [[nodiscard]] bool IsFocused() const noexcept;
    void SwapBuffers();
    [[nodiscard]] bool AttachNativeWindow(void* window);
    void DetachNativeWindow();
    [[nodiscard]] bool TryRecoverSurface() noexcept;
    void SetVSyncEnabled(bool enabled);
    [[nodiscard]] bool IsVSyncEnabled() const noexcept;
    [[nodiscard]] std::uint32_t GetWidth() const noexcept;
    [[nodiscard]] std::uint32_t GetHeight() const noexcept;
    [[nodiscard]] void* GetNativeWindowHandle() const noexcept;
    [[nodiscard]] std::uint64_t GetPresentationCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Window
