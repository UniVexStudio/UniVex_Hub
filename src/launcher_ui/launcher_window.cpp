// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "launcher_window.h"

#include <cstdio>
#include <exception>
#include <span>

#include "editor_host.h"
#include "glfw_window_handle.h"
#include "hub_window_host.h"
#include "logging_macros.h"
#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Theme.h"
#include "prism/WindowChrome.h"

namespace UVE::LauncherUI {
namespace {

inline constexpr std::uint32_t kFocusedFrameRateCapUVE = 60U;
inline constexpr std::uint32_t kUnfocusedFrameRateCapUVE = 10U;

} // namespace

void RequestLauncherCloseUVE() {
    if (prism::Application::exists()) {
        prism::Application::get().quit();
    }
}

int RunLauncherWindowUVE(const LauncherWindowDescUVE& desc,
                         const std::function<prism::Ref<prism::Widget>()>& buildRoot,
                         const std::function<void()>& tick) {
    Host::HubWindowConfigUVE config{};
    config.title = desc.title;
    config.width = desc.width;
    config.height = desc.height;
    config.borderless = true;
    config.logFile = desc.logFile;
#if defined(__ANDROID__)
    config.focusedFrameRateCap = 0U;
#else
    config.focusedFrameRateCap = kFocusedFrameRateCapUVE;
#endif
    config.unfocusedFrameRateCap = kUnfocusedFrameRateCapUVE;
    config.nativeSurface = desc.nativeSurface;

    Host::HubWindowHostUVE windowHost(config);
    try {
        if (!windowHost.InitializeUVE()) {
            UVE_ERROR("{}: no window could be created", desc.title);
            return kLauncherStartupFailedExitCodeUVE;
        }
        void* const nativeWindow = windowHost.GetNativeWindowHandleUVE();
        if (nativeWindow == nullptr) {
            UVE_ERROR("{}: no window handle was returned", desc.title);
            windowHost.ShutdownUVE();
            return kLauncherStartupFailedExitCodeUVE;
        }
        Editor::EditorHostUVE uiHost;
        if (!uiHost.InitializeUVE(Window::GlfwWindowFromNativeHandleUVE(nativeWindow), desc.prismAssetsDirectory)) {
            UVE_ERROR("{}: the interface could not start", desc.title);
            windowHost.ShutdownUVE();
            return kLauncherStartupFailedExitCodeUVE;
        }

        auto safeFrame = prism::make<prism::Border>();
        safeFrame->setBackground(&prism::Theme::panel);
        safeFrame->setChild(buildRoot());
        prism::Margin safeArea;
        if (desc.windowChrome) {
            prism::WindowChromeOptions chrome;
            chrome.title = desc.title;
            chrome.showIdentity = desc.showTitleInChrome;
            uiHost.SetRootUVE(prism::wrapWithWindowChrome(safeFrame, Editor::EditorHostUVE::kMainWindowUVE, chrome));
        } else {
            uiHost.SetRootUVE(safeFrame);
        }

        int framesRun = 0;
        while (!uiHost.QuitRequestedUVE() && !windowHost.IsCloseRequestedUVE() &&
               (!desc.frameLimit.has_value() || framesRun < *desc.frameLimit)) {
            if (desc.beforeFrame && !desc.beforeFrame(windowHost)) {
                break;
            }
            windowHost.BeginFrameUVE();
            if (desc.safeArea) {
                const prism::Margin area = desc.safeArea();
                if (area.l != safeArea.l || area.t != safeArea.t || area.r != safeArea.r || area.b != safeArea.b) {
                    safeArea = area;
                    safeFrame->setPadding(area);
                }
            }
            if (tick) {
                tick();
            }
            uiHost.BeginFrameUVE();
            uiHost.RenderUVE(std::span<const Editor::EditorHostUVE::ViewImageUVE>{});
            windowHost.EndFrameUVE();
            ++framesRun;
        }
        if (!desc.screenshot.empty() && !uiHost.SaveScreenshotUVE(desc.screenshot)) {
            std::fprintf(stderr, "%s: could not save the screenshot\n", desc.title.c_str());
        }
        uiHost.ShutdownUVE();
        windowHost.ShutdownUVE();
        return 0;
    } catch (const std::exception& exception) {
        UVE_FATAL("launcher: unhandled exception - shutting down: {}", exception.what());
    } catch (...) {
        UVE_FATAL("launcher: unhandled non-std::exception - shutting down");
    }
    if (windowHost.IsRunningUVE()) {
        try {
            windowHost.ShutdownUVE();
        } catch (...) {
            UVE_FATAL("launcher: shutting down after an exception failed as well");
        }
    }
    return Host::HubWindowHostUVE::kUnhandledExceptionExitCodeUVE;
}

} // namespace UVE::LauncherUI
