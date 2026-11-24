// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// UniVex Hub on Android: the desktop Hub window running in a NativeActivity. The APK's data
// (PRISM fonts/themes/brand mark, project templates, engine feed) is unpacked into app-private
// storage, which also holds the project list and downloaded engines.
//
// Opening a project runs the engine's editor in this same process: Android starts no second
// program, so the Hub closes its window, loads the engine's editor library via dlopen, checks the
// optional ABI v1 version symbol, and calls UveEditorRunUVE on this activity.

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

using UVE::AndroidHost::kLogTagUVE;

inline constexpr std::string_view kUnpackedAppDirectoryUVE = "app";
inline constexpr const char* kHubRenderedMarkerUVE = "UVE_HUB_RENDERED";

enum class HubOutcomeUVE { Quit, OpenEditor };

[[nodiscard]] HubOutcomeUVE RunHubUVE(UVE::AndroidHost::ActivityStateUVE& activity, const std::filesystem::path& unpacked,
                                      const std::string& initialError) {
    android_app* const app = activity.app;
    UVE::Hub::HubContextUVE context;
    context.templatesDirectory = unpacked / UVE::Pack::kProjectTemplatesDirectoryNameUVE;
    std::string registryError;
    const std::optional<std::filesystem::path> registry = UVE::Pack::DefaultProjectRegistryFileUVE(&registryError);
    if (!registry.has_value()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_FAILED %s", registryError.c_str());
        return HubOutcomeUVE::Quit;
    }
    context.registryFile = *registry;
    context.settingsFile = context.registryFile.parent_path() / UVE::Pack::kLauncherSettingsFileNameUVE;
    context.enginesRoot = context.registryFile.parent_path() / UVE::Online::kEnginesDirectoryNameUVE;
    context.feed = UVE::Online::LoadEngineFeedSourceUVE(unpacked / UVE::Online::kEngineFeedFileNameUVE);
    context.compactLayout = true;

    UVE::LauncherUI::LauncherWindowDescUVE window;
    window.title = std::string(UVE::Hub::kHubWindowTitleUVE);
    window.windowChrome = false;
    window.safeArea = [&activity] {
        const float scale = prism::Application::exists() ? prism::Application::get().dpiScale() : 1.0F;
        const float invScale = scale > 0.0F ? (1.0F / scale) : 1.0F;
        const UVE::Window::AndroidSafeAreaUVE& area = activity.safeArea;
        return prism::Margin(0.0F, 0.0F, 0.0F, static_cast<float>(area.imeBottom) * invScale);
    };
    window.nativeSurface = app->window;
    window.prismAssetsDirectory = unpacked / UVE::LauncherUI::kPrismAssetsDirectoryNameUVE;
    window.logFile = context.registryFile.parent_path() / UVE::Hub::kHubLogFileNameUVE;
    window.beforeFrame = [&activity](UVE::Host::HubWindowHostUVE& host) {
        const bool uiBusy = prism::Application::exists() && prism::Application::get().needsRender();
        const int idleWaitMs = uiBusy ? 0 : 50;
        return UVE::AndroidHost::BeforeFrameUVE(activity, host, kHubRenderedMarkerUVE, idleWaitMs);
    };

    UVE::LauncherUI::SetSystemFilePickerUVE(
        [&activity](const std::vector<std::string>& suffixes, std::function<void(const std::filesystem::path&)> onPicked,
                    std::function<void(const std::string&)> onError) {
            return UVE::AndroidHost::StartFilePickUVE(activity, suffixes, std::move(onPicked), std::move(onError));
        });
    UVE::LauncherUI::SetStorageAccessProviderUVE(
        [app] { return UVE::Window::HasAndroidAllFilesAccessUVE(app->activity); },
        [app] { UVE::Window::RequestAndroidAllFilesAccessUVE(app->activity); });
    UVE::Hub::HubWindowUVE hub(std::move(context));
    __android_log_print(ANDROID_LOG_INFO, kLogTagUVE, "UVE_HUB_STARTED");
    const int exitCode = UVE::LauncherUI::RunLauncherWindowUVE(
        window,
        [&hub, &initialError] {
            auto root = hub.BuildUVE();
            if (!initialError.empty()) {
                hub.ShowMessageUVE(initialError, true);
            }
            return root;
        },
        [&hub] { hub.TickUVE(); });
    UVE::LauncherUI::SetSystemFilePickerUVE({});
    UVE::LauncherUI::SetStorageAccessProviderUVE({}, {});
    activity.pendingPick = nullptr;
    activity.pendingPickError = nullptr;
    UVE::AndroidHost::DetachEngineUVE(activity);
    __android_log_print(ANDROID_LOG_INFO, kLogTagUVE, "UVE_HUB_STOPPED %d", exitCode);
    return app->destroyRequested == 0 && exitCode == 0 ? HubOutcomeUVE::OpenEditor : HubOutcomeUVE::Quit;
}

[[nodiscard]] std::string RunEditorUVE(android_app* const app, const UVE::Platform::ProcessLaunchDescUVE& launch) {
    // Keep the handle loaded for the life of the process (RTLD_LOCAL isolates symbols).
    void* const library = dlopen(launch.executable.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        const char* const reason = dlerror();
        const std::string message = std::string("Could not load the engine editor: ") +
                                    (reason != nullptr ? reason : "unknown loader error");
        __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_EDITOR_FAILED %s", message.c_str());
        return message;
    }

    // Optional ABI version check: if exported, must match UVE_ANDROID_EDITOR_ABI_VERSION (v1).
    if (const auto abiVersionFn =
            reinterpret_cast<UveEditorAbiVersionFnUVE>(dlsym(library, UVE_ANDROID_EDITOR_ABI_VERSION_SYMBOL));
        abiVersionFn != nullptr) {
        const std::uint32_t abiVersion = abiVersionFn();
        if (abiVersion != UVE_ANDROID_EDITOR_ABI_VERSION) {
            const std::string message = "Incompatible Android editor ABI version " + std::to_string(abiVersion) +
                                        " (Hub requires v" + std::to_string(UVE_ANDROID_EDITOR_ABI_VERSION) + ").";
            __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_EDITOR_FAILED %s", message.c_str());
            return message;
        }
    }

    const auto entry = reinterpret_cast<UveEditorRunFnUVE>(dlsym(library, UVE_ANDROID_EDITOR_ENTRY_SYMBOL));
    if (entry == nullptr) {
        const char* const reason = dlerror();
        const std::string message = std::string("The installed engine package has no editor entry point: ") +
                                    (reason != nullptr ? reason : "symbol not found");
        __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_EDITOR_FAILED %s: %s",
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
    __android_log_print(ANDROID_LOG_INFO, kLogTagUVE, "UVE_HUB_EDITOR_STARTED %s", launch.executable.c_str());
    const int exitCode = entry(app, static_cast<int>(arguments.size()), argv.data());
    __android_log_print(ANDROID_LOG_INFO, kLogTagUVE, "UVE_HUB_EDITOR_STOPPED %d", exitCode);
    UVE::AndroidHost::EnterDataDirectoryUVE(app);
    if (exitCode != 0) {
        return "The editor exited with code " + std::to_string(exitCode) + ". Check uve_editor.log for details.";
    }
    return {};
}

} // namespace

extern "C" void android_main(android_app* const app) {
    UVE::AndroidHost::ActivityStateUVE activity;
    UVE::AndroidHost::AttachUVE(activity, app);
    if (!UVE::AndroidHost::WaitForWindowUVE(activity)) {
        return;
    }
    UVE::AndroidHost::EnterDataDirectoryUVE(app);
    const std::filesystem::path unpacked = std::filesystem::path(app->activity->internalDataPath) / kUnpackedAppDirectoryUVE;
    if (!UVE::AndroidHost::UnpackAssetsUVE(app->activity->assetManager, unpacked)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_FAILED the APK's data could not be unpacked");
        return;
    }

    try {
        std::string lastEditorError;
        const std::string launchProject = UVE::Window::TakeAndroidLaunchProjectUVE(app->activity);
        if (!launchProject.empty()) {
            std::string registryError;
            const std::optional<std::filesystem::path> registry =
                UVE::Pack::DefaultProjectRegistryFileUVE(&registryError);
            if (!registry.has_value()) {
                lastEditorError = registryError;
            } else {
                const std::filesystem::path enginesRoot =
                    registry->parent_path() / UVE::Online::kEnginesDirectoryNameUVE;
                const UVE::Hub::ProjectOpenResultUVE opened = UVE::Hub::OpenProjectInEngineUVE(
                    std::filesystem::path(launchProject), UVE::Online::ListInstalledEnginesUVE(enginesRoot, std::nullopt),
                    *registry, {});
                if (!opened.launched) {
                    lastEditorError = opened.message;
                    __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_LAUNCH_FAILED %s", opened.message.c_str());
                } else if (const std::optional<UVE::Platform::ProcessLaunchDescUVE> launch =
                               UVE::Hub::TakePendingEditorLaunchUVE();
                           launch.has_value()) {
                    lastEditorError = RunEditorUVE(app, *launch);
                    UVE::AndroidHost::AttachUVE(activity, app);
                    if (!UVE::AndroidHost::WaitForWindowUVE(activity)) {
                        ANativeActivity_finish(app->activity);
                        UVE::AndroidHost::DrainUntilDestroyedUVE(app);
                        return;
                    }
                }
            }
        }
        while (app->destroyRequested == 0) {
            if (RunHubUVE(activity, unpacked, lastEditorError) != HubOutcomeUVE::OpenEditor) {
                break;
            }
            lastEditorError.clear();
            const std::optional<UVE::Platform::ProcessLaunchDescUVE> launch = UVE::Hub::TakePendingEditorLaunchUVE();
            if (!launch.has_value()) {
                break;
            }
            lastEditorError = RunEditorUVE(app, *launch);
            UVE::AndroidHost::AttachUVE(activity, app);
            if (!UVE::AndroidHost::WaitForWindowUVE(activity)) {
                break;
            }
        }
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_FAILED %s", error.what());
    }
    ANativeActivity_finish(app->activity);
    UVE::AndroidHost::DrainUntilDestroyedUVE(app);
}
