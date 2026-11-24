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

inline constexpr std::uint32_t kFocusedFrameRateCap = 60U;
inline constexpr std::uint32_t kUnfocusedFrameRateCap = 10U;

} // namespace

void RequestLauncherClose() {
    if (prism::Application::exists()) {
        prism::Application::get().quit();
    }
}

int RunLauncherWindow(const LauncherWindowDesc& desc,
                         const std::function<prism::Ref<prism::Widget>()>& buildRoot,
                         const std::function<void()>& tick) {
    Host::HubWindowConfig config{};
    config.title = desc.title;
    config.width = desc.width;
    config.height = desc.height;
    config.borderless = true;
    config.logFile = desc.logFile;
#if defined(__ANDROID__)
    config.focusedFrameRateCap = 0U;
#else
    config.focusedFrameRateCap = kFocusedFrameRateCap;
#endif
    config.unfocusedFrameRateCap = kUnfocusedFrameRateCap;
    config.nativeSurface = desc.nativeSurface;

    Host::HubWindowHost windowHost(config);
    try {
        if (!windowHost.Initialize()) {
            UVE_ERROR("{}: no window could be created", desc.title);
            return kLauncherStartupFailedExitCode;
        }
        void* const nativeWindow = windowHost.GetNativeWindowHandle();
        if (nativeWindow == nullptr) {
            UVE_ERROR("{}: no window handle was returned", desc.title);
            windowHost.Shutdown();
            return kLauncherStartupFailedExitCode;
        }
        Editor::EditorHost uiHost;
        if (!uiHost.Initialize(Window::GlfwWindowFromNativeHandle(nativeWindow), desc.prismAssetsDirectory)) {
            UVE_ERROR("{}: the interface could not start", desc.title);
            windowHost.Shutdown();
            return kLauncherStartupFailedExitCode;
        }

        auto safeFrame = prism::make<prism::Border>();
        safeFrame->setBackground(&prism::Theme::panel);
        safeFrame->setChild(buildRoot());
        prism::Margin safeArea;
        if (desc.windowChrome) {
            prism::WindowChromeOptions chrome;
            chrome.title = desc.title;
            chrome.showIdentity = desc.showTitleInChrome;
            uiHost.SetRoot(prism::wrapWithWindowChrome(safeFrame, Editor::EditorHost::kMainWindow, chrome));
        } else {
            uiHost.SetRoot(safeFrame);
        }

        int framesRun = 0;
        while (!uiHost.QuitRequested() && !windowHost.IsCloseRequested() &&
               (!desc.frameLimit.has_value() || framesRun < *desc.frameLimit)) {
            if (desc.beforeFrame && !desc.beforeFrame(windowHost)) {
                break;
            }
            windowHost.BeginFrame();
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
            uiHost.BeginFrame();
            uiHost.Render(std::span<const Editor::EditorHost::ViewImage>{});
            windowHost.EndFrame();
            ++framesRun;
        }
        if (!desc.screenshot.empty() && !uiHost.SaveScreenshot(desc.screenshot)) {
            std::fprintf(stderr, "%s: could not save the screenshot\n", desc.title.c_str());
        }
        uiHost.Shutdown();
        windowHost.Shutdown();
        return 0;
    } catch (const std::exception& exception) {
        UVE_FATAL("launcher: unhandled exception - shutting down: {}", exception.what());
    } catch (...) {
        UVE_FATAL("launcher: unhandled non-std::exception - shutting down");
    }
    if (windowHost.IsRunning()) {
        try {
            windowHost.Shutdown();
        } catch (...) {
            UVE_FATAL("launcher: shutting down after an exception failed as well");
        }
    }
    return Host::HubWindowHost::kUnhandledExceptionExitCode;
}

} // namespace UVE::LauncherUI
