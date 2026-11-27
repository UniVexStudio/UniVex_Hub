// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// UniVex Hub on Android: the desktop Hub window running in a NativeActivity. The APK's data
// (PRISM fonts/themes/brand mark, project templates, engine feed) is unpacked into app-private
// storage, which also holds the project list and downloaded engines.
//
// Opening a project runs the engine's editor in this same process: Android starts no second
// program, so the Hub closes its window, loads the engine's editor library via dlopen, checks the
// optional ABI v1 version symbol, and calls UveEditorRun on this activity.

#include <android/log.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>

#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "android_host.h"
#include "engine_install.h"
#include "engine_release_feed.h"
#include "folder_picker.h"
#include "hub_project_opener.h"
#include "hub_window.h"
#include "hub_window_host.h"
#include "launcher_window.h"
#include "prism/Application.h"
#include "project_list.h"
#include "project_registry.h"
#include "univex_android_editor_abi.h"

namespace {

using UVE::AndroidHost::kLogTag;

inline constexpr std::string_view kUnpackedAppDirectory = "app";
inline constexpr const char* kHubRenderedMarker = "UVE_HUB_RENDERED";

enum class HubOutcome { Quit, OpenEditor };

[[nodiscard]] HubOutcome RunHub(UVE::AndroidHost::ActivityState& activity, const std::filesystem::path& unpacked,
                                      const std::string& initialError) {
    android_app* const app = activity.app;
    UVE::Hub::HubContext context;
    context.templatesDirectory = unpacked / UVE::Pack::kProjectTemplatesDirectoryName;
    std::string registryError;
    const std::optional<std::filesystem::path> registry = UVE::Pack::DefaultProjectRegistryFile(&registryError);
    if (!registry.has_value()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "UVE_HUB_FAILED %s", registryError.c_str());
        return HubOutcome::Quit;
    }
    context.registryFile = *registry;
    context.settingsFile = context.registryFile.parent_path() / UVE::Pack::kLauncherSettingsFileName;
    context.enginesRoot = context.registryFile.parent_path() / UVE::Online::kEnginesDirectoryName;
    context.feed = UVE::Online::LoadEngineFeedSource(unpacked / UVE::Online::kEngineFeedFileName);
    // Unified PC visual layout on mobile (users can zoom cleanly via Hub Settings presets).
    context.compactLayout = false;

    UVE::LauncherUI::LauncherWindowDesc window;
    window.title = std::string(UVE::Hub::kHubWindowTitle);
    window.windowChrome = false;
    window.safeArea = [&activity] {
        const float scale = prism::Application::exists() ? prism::Application::get().dpiScale() : 1.0F;
        const float invScale = scale > 0.0F ? (1.0F / scale) : 1.0F;
        const UVE::Window::AndroidSafeArea& area = activity.safeArea;
        return prism::Margin(0.0F, 0.0F, 0.0F, static_cast<float>(area.imeBottom) * invScale);
    };
    window.nativeSurface = app->window;
    window.prismAssetsDirectory = unpacked / UVE::LauncherUI::kPrismAssetsDirectoryName;
    window.logFile = context.registryFile.parent_path() / UVE::Hub::kHubLogFileName;
    window.beforeFrame = [&activity](UVE::Host::HubWindowHost& host) {
        const bool uiBusy = prism::Application::exists() && prism::Application::get().needsRender();
        const int idleWaitMs = uiBusy ? 0 : 50;
        return UVE::AndroidHost::BeforeFrame(activity, host, kHubRenderedMarker, idleWaitMs);
    };

    UVE::LauncherUI::SetSystemFilePicker(
        [&activity](const std::vector<std::string>& suffixes, std::function<void(const std::filesystem::path&)> onPicked,
                    std::function<void(const std::string&)> onError) {
            return UVE::AndroidHost::StartFilePick(activity, suffixes, std::move(onPicked), std::move(onError));
        });
    UVE::LauncherUI::SetStorageAccessProvider(
        [app] { return UVE::Window::HasAndroidAllFilesAccess(app->activity); },
        [app] { UVE::Window::RequestAndroidAllFilesAccess(app->activity); });
    UVE::Hub::HubWindow hub(std::move(context));
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "UVE_HUB_STARTED");
    const int exitCode = UVE::LauncherUI::RunLauncherWindow(
        window,
        [&hub, &initialError] {
            auto root = hub.Build();
            if (!initialError.empty()) {
                hub.ShowMessage(initialError, true);
            }
            return root;
        },
        [&hub] { hub.Tick(); });
    UVE::LauncherUI::SetSystemFilePicker({});
    UVE::LauncherUI::SetStorageAccessProvider({}, {});
    activity.pendingPick = nullptr;
    activity.pendingPickError = nullptr;
    UVE::AndroidHost::DetachEngine(activity);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "UVE_HUB_STOPPED %d", exitCode);
    return app->destroyRequested == 0 && exitCode == 0 ? HubOutcome::OpenEditor : HubOutcome::Quit;
}

[[nodiscard]] std::string RunEditor(android_app* const app, const UVE::Platform::ProcessLaunchDesc& launch) {
    // Keep the handle loaded for the life of the process (RTLD_LOCAL isolates symbols).
    void* const library = dlopen(launch.executable.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        const char* const reason = dlerror();
        const std::string message = std::string("Could not load the engine editor: ") +
                                    (reason != nullptr ? reason : "unknown loader error");
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "UVE_HUB_EDITOR_FAILED %s", message.c_str());
        return message;
    }

    // Probe ABI v2 first (UveEditorRun / UveEditorAbiVersion), then fall back to legacy v1 packages.
    auto entry = reinterpret_cast<UveEditorRunFn>(dlsym(library, UVE_ANDROID_EDITOR_ENTRY_SYMBOL));
    if (entry != nullptr) {
        if (const auto abiVersionFn =
                reinterpret_cast<UveEditorAbiVersionFn>(dlsym(library, UVE_ANDROID_EDITOR_ABI_VERSION_SYMBOL));
            abiVersionFn != nullptr) {
            const std::uint32_t abiVersion = abiVersionFn();
            if (abiVersion != UVE_ANDROID_EDITOR_ABI_VERSION) {
                const std::string message = "Incompatible Android editor ABI version " + std::to_string(abiVersion) +
                                            " (Hub requires v" + std::to_string(UVE_ANDROID_EDITOR_ABI_VERSION) + ").";
                __android_log_print(ANDROID_LOG_ERROR, kLogTag, "UVE_HUB_EDITOR_FAILED %s", message.c_str());
                return message;
            }
        }
    } else {
        entry = reinterpret_cast<UveEditorRunFn>(dlsym(library, UVE_ANDROID_EDITOR_ENTRY_SYMBOL_V1));
        if (entry != nullptr) {
            if (const auto abiVersionFn = reinterpret_cast<UveEditorAbiVersionFn>(
                    dlsym(library, UVE_ANDROID_EDITOR_ABI_VERSION_SYMBOL_V1));
                abiVersionFn != nullptr) {
                const std::uint32_t abiVersion = abiVersionFn();
                if (abiVersion != UVE_ANDROID_EDITOR_ABI_VERSION_V1) {
                    const std::string message = "Incompatible legacy Android editor ABI version " +
                                                std::to_string(abiVersion) + " (expected v" +
                                                std::to_string(UVE_ANDROID_EDITOR_ABI_VERSION_V1) + ").";
                    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "UVE_HUB_EDITOR_FAILED %s", message.c_str());
                    return message;
                }
            }
        }
    }

    if (entry == nullptr) {
        const char* const reason = dlerror();
        const std::string message = std::string("The installed engine package has no editor entry point: ") +
                                    (reason != nullptr ? reason : "symbol not found");
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "UVE_HUB_EDITOR_FAILED %s: %s",
                            launch.executable.c_str(), message.c_str());
        return message;
    }
    std::vector<std::string> arguments{launch.executable.string()};
    arguments.insert(arguments.end(), launch.arguments.begin(), launch.arguments.end());
    std::vector<char*> argv;
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    std::error_code error;
    if (!launch.workingDirectory.empty()) {
        std::filesystem::current_path(launch.workingDirectory, error);
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "UVE_HUB_EDITOR_STARTED %s", launch.executable.c_str());
    const int exitCode = entry(app, static_cast<int>(arguments.size()), argv.data());
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "UVE_HUB_EDITOR_STOPPED %d", exitCode);
    UVE::AndroidHost::EnterDataDirectory(app);
    if (exitCode != 0) {
        return "The editor exited with code " + std::to_string(exitCode) + ". Check uve_editor.log for details.";
    }
    return {};
}

} // namespace

extern "C" void android_main(android_app* const app) {
    UVE::AndroidHost::ActivityState activity;
    UVE::AndroidHost::Attach(activity, app);
    if (!UVE::AndroidHost::WaitForWindow(activity)) {
        return;
    }
    UVE::AndroidHost::EnterDataDirectory(app);
    const std::filesystem::path unpacked = std::filesystem::path(app->activity->internalDataPath) / kUnpackedAppDirectory;
    if (!UVE::AndroidHost::UnpackAssets(app->activity->assetManager, unpacked)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "UVE_HUB_FAILED the APK's data could not be unpacked");
        return;
    }

    try {
        std::string lastEditorError;
        const std::string launchProject = UVE::Window::TakeAndroidLaunchProject(app->activity);
        if (!launchProject.empty()) {
            std::string registryError;
            const std::optional<std::filesystem::path> registry =
                UVE::Pack::DefaultProjectRegistryFile(&registryError);
            if (!registry.has_value()) {
                lastEditorError = registryError;
            } else {
                const std::filesystem::path enginesRoot =
                    registry->parent_path() / UVE::Online::kEnginesDirectoryName;
                const UVE::Hub::ProjectOpenResult opened = UVE::Hub::OpenProjectInEngine(
                    std::filesystem::path(launchProject), UVE::Online::ListInstalledEngines(enginesRoot, std::nullopt),
                    *registry, {});
                if (!opened.launched) {
                    lastEditorError = opened.message;
                    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "UVE_HUB_LAUNCH_FAILED %s", opened.message.c_str());
                } else if (const std::optional<UVE::Platform::ProcessLaunchDesc> launch =
                               UVE::Hub::TakePendingEditorLaunch();
                           launch.has_value()) {
                    lastEditorError = RunEditor(app, *launch);
                    UVE::AndroidHost::Attach(activity, app);
                    if (!UVE::AndroidHost::WaitForWindow(activity)) {
                        ANativeActivity_finish(app->activity);
                        UVE::AndroidHost::DrainUntilDestroyed(app);
                        return;
                    }
                }
            }
        }
        while (app->destroyRequested == 0) {
            if (RunHub(activity, unpacked, lastEditorError) != HubOutcome::OpenEditor) {
                break;
            }
            lastEditorError.clear();
            const std::optional<UVE::Platform::ProcessLaunchDesc> launch = UVE::Hub::TakePendingEditorLaunch();
            if (!launch.has_value()) {
                break;
            }
            lastEditorError = RunEditor(app, *launch);
            UVE::AndroidHost::Attach(activity, app);
            if (!UVE::AndroidHost::WaitForWindow(activity)) {
                break;
            }
        }
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "UVE_HUB_FAILED %s", error.what());
    }
    ANativeActivity_finish(app->activity);
    UVE::AndroidHost::DrainUntilDestroyed(app);
}
