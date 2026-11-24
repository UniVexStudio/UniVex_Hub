// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hub_window_host.h"

#include <array>
#include <chrono>
#include <thread>
#include <utility>

#include "gl_system_headers.h"
#include "logging_macros.h"

#if defined(__ANDROID__)
#include "android_window_manager.h"
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

namespace UVE::Host {
namespace {

#if !defined(__ANDROID__)
int g_glfwRefCount = 0;
bool g_suppressGlfwErrors = false;

void GlfwErrorCallbackUVE(const int errorCode, const char* const description) {
    if (g_suppressGlfwErrors) {
        return;
    }
    UVE_ERROR("HubWindowHostUVE: GLFW error {}: {}", errorCode,
              description != nullptr ? description : "(no description)");
}
#endif

} // namespace

struct HubWindowHostUVE::Impl {
    explicit Impl(HubWindowConfigUVE cfg) : config(std::move(cfg)) {}

    HubWindowConfigUVE config;
    bool running = false;
    std::chrono::steady_clock::time_point frameStart{};

#if defined(__ANDROID__)
    std::unique_ptr<Window::AndroidWindowManagerUVE> androidWindow;
#else
    GLFWwindow* window = nullptr;
    bool ownsGlfwRef = false;
    bool focused = true;
#endif
};

HubWindowHostUVE::HubWindowHostUVE(HubWindowConfigUVE config)
    : m_impl(std::make_unique<Impl>(std::move(config))) {}

HubWindowHostUVE::~HubWindowHostUVE() {
    ShutdownUVE();
}

bool HubWindowHostUVE::InitializeUVE() {
    if (m_impl->running) {
        return true;
    }
    if (!m_impl->config.logFile.empty()) {
        Logging::SetLogFileUVE(m_impl->config.logFile);
    }

#if defined(__ANDROID__)
    m_impl->androidWindow = std::make_unique<Window::AndroidWindowManagerUVE>(m_impl->config.nativeSurface, true);
    if (!m_impl->androidWindow->IsValidUVE()) {
        UVE_ERROR("HubWindowHostUVE: failed to initialize Android EGL/GLES3 window");
        m_impl->androidWindow.reset();
        return false;
    }
    m_impl->running = true;
    return true;
#else
    if (g_glfwRefCount == 0) {
        glfwSetErrorCallback(&GlfwErrorCallbackUVE);
        if (glfwInit() != GLFW_TRUE) {
            UVE_FATAL("HubWindowHostUVE: glfwInit() failed");
            return false;
        }
    }
    ++g_glfwRefCount;
    m_impl->ownsGlfwRef = true;

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, m_impl->config.borderless ? GLFW_FALSE : GLFW_TRUE);
#ifdef GLFW_SCALE_TO_MONITOR
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#endif

    struct GlVersion {
        int major;
        int minor;
    };
    constexpr std::array<GlVersion, 4> kVersions{{{4, 3}, {4, 1}, {3, 3}, {3, 2}}};

    const int width = static_cast<int>(m_impl->config.width > 0U ? m_impl->config.width : 1200U);
    const int height = static_cast<int>(m_impl->config.height > 0U ? m_impl->config.height : 760U);

    g_suppressGlfwErrors = true;
    for (const GlVersion ver : kVersions) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, ver.major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, ver.minor);
        m_impl->window = glfwCreateWindow(width, height, m_impl->config.title.c_str(), nullptr, nullptr);
        if (m_impl->window != nullptr) {
            break;
        }
    }
    g_suppressGlfwErrors = false;

    if (m_impl->window == nullptr) {
        UVE_ERROR("HubWindowHostUVE: glfwCreateWindow() failed");
        ShutdownUVE();
        return false;
    }

    glfwMakeContextCurrent(m_impl->window);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(m_impl->window, m_impl.get());
    glfwSetWindowFocusCallback(m_impl->window, [](GLFWwindow* const win, const int isFocused) {
        if (auto* const impl = static_cast<Impl*>(glfwGetWindowUserPointer(win))) {
            impl->focused = (isFocused == GLFW_TRUE);
        }
    });

    m_impl->running = true;
    return true;
#endif
}

void HubWindowHostUVE::ShutdownUVE() {
    m_impl->running = false;
#if defined(__ANDROID__)
    m_impl->androidWindow.reset();
#else
    if (m_impl->window != nullptr) {
        glfwDestroyWindow(m_impl->window);
        m_impl->window = nullptr;
    }
    if (m_impl->ownsGlfwRef) {
        m_impl->ownsGlfwRef = false;
        --g_glfwRefCount;
        if (g_glfwRefCount <= 0) {
            g_glfwRefCount = 0;
            glfwTerminate();
        }
    }
#endif
    Logging::CloseLogFileUVE();
}

bool HubWindowHostUVE::IsRunningUVE() const noexcept { return m_impl->running; }

bool HubWindowHostUVE::IsCloseRequestedUVE() const noexcept {
#if defined(__ANDROID__)
    return !m_impl->androidWindow || !m_impl->androidWindow->IsValidUVE();
#else
    return m_impl->window == nullptr || glfwWindowShouldClose(m_impl->window) == GLFW_TRUE;
#endif
}

void* HubWindowHostUVE::GetNativeWindowHandleUVE() const noexcept {
#if defined(__ANDROID__)
    return m_impl->androidWindow ? m_impl->androidWindow->GetNativeWindowHandleUVE() : nullptr;
#else
    return m_impl->window;
#endif
}

Window::AndroidWindowManagerUVE* HubWindowHostUVE::GetAndroidWindowManagerUVE() noexcept {
#if defined(__ANDROID__)
    return m_impl->androidWindow.get();
#else
    return nullptr;
#endif
}

void HubWindowHostUVE::BeginFrameUVE() {
    m_impl->frameStart = std::chrono::steady_clock::now();
#if !defined(__ANDROID__)
    if (m_impl->window != nullptr) {
        glfwPollEvents();
        glfwMakeContextCurrent(m_impl->window);
    }
#endif
    glClearColor(0.08F, 0.09F, 0.11F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
}

void HubWindowHostUVE::EndFrameUVE() {
#if defined(__ANDROID__)
    if (m_impl->androidWindow) {
        m_impl->androidWindow->SwapBuffersUVE();
    }
#else
    if (m_impl->window != nullptr) {
        glfwSwapBuffers(m_impl->window);
    }
    const std::uint32_t cap = m_impl->focused ? m_impl->config.focusedFrameRateCap
                                              : m_impl->config.unfocusedFrameRateCap;
    if (cap > 0U) {
        const auto target = std::chrono::microseconds(1'000'000U / cap);
        const auto elapsed = std::chrono::steady_clock::now() - m_impl->frameStart;
        if (elapsed < target) {
            std::this_thread::sleep_for(target - elapsed);
        }
    }
#endif
}

} // namespace UVE::Host
