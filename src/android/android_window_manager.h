// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>

namespace UVE::Window {

/// Standalone Android EGL / OpenGL ES 3 surface manager for UniVex Hub.
/// Handles fallback EGL config selection, 1x1 pbuffer idle surface during detach,
/// surface reattachment on APP_CMD_INIT_WINDOW, and VSync swap interval.
class AndroidWindowManagerUVE final {
public:
    AndroidWindowManagerUVE(void* nativeWindow, bool vsync = true);
    ~AndroidWindowManagerUVE();
    AndroidWindowManagerUVE(const AndroidWindowManagerUVE&) = delete;
    AndroidWindowManagerUVE& operator=(const AndroidWindowManagerUVE&) = delete;

    [[nodiscard]] bool IsValidUVE() const noexcept;
    void SetFocusedUVE(bool focused) noexcept;
    [[nodiscard]] bool IsFocusedUVE() const noexcept;
    void SwapBuffersUVE();
    [[nodiscard]] bool AttachNativeWindowUVE(void* window);
    void DetachNativeWindowUVE();
    [[nodiscard]] bool TryRecoverSurfaceUVE() noexcept;
    void SetVSyncEnabledUVE(bool enabled);
    [[nodiscard]] bool IsVSyncEnabledUVE() const noexcept;
    [[nodiscard]] std::uint32_t GetWidthUVE() const noexcept;
    [[nodiscard]] std::uint32_t GetHeightUVE() const noexcept;
    [[nodiscard]] void* GetNativeWindowHandleUVE() const noexcept;
    [[nodiscard]] std::uint64_t GetPresentationCountUVE() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Window
