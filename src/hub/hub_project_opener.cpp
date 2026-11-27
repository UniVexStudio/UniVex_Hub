// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hub_project_opener.h"

#include <chrono>
#include <utility>

#include "editor_project_package.h"
#include "engine_install.h"
#include "launcher_window.h"
#include "process_launcher.h"
#include "project_compatibility.h"

namespace UVE::Hub {
namespace {

/// The editor launch Android carries out after the Hub's window closes.
std::optional<Platform::ProcessLaunchDesc> g_pendingEditorLaunch;

} // namespace

std::optional<Platform::ProcessLaunchDesc> TakePendingEditorLaunch() {
    return std::exchange(g_pendingEditorLaunch, std::nullopt);
}

ProjectOpenResult OpenProjectInEngine(const std::filesystem::path& projectFile,
                                            const std::vector<Online::InstalledEngine>& engines,
                                            const std::filesystem::path& registryFile,
                                            const std::filesystem::path& registryOverride,
                                            const float editorZoom) {
    if (projectFile.empty()) {
        return {false, "Enter the path of a project file to open."};
    }
    const Platform::EditorProjectPackageLoadResult manifest = Platform::EditorProjectPackageCodec::Load(projectFile);
    if (!manifest.IsAccepted()) {
        return {false, "Cannot open " + projectFile.string() + ": " + manifest.result.message};
    }
    const Platform::EditorProjectPackage& package = *manifest.package;

    Pack::ProjectRegistry registry;
    const bool registryLoaded = registry.Load(registryFile).IsApplied();
    const Pack::ProjectRegistryEntry* const entry = registryLoaded ? registry.Find(projectFile) : nullptr;
    const Online::InstalledEngine* const engine = Online::ChooseEngineForProject(
        engines, package.engineVersion, entry != nullptr ? entry->engineChoice : std::string{});

    // Listed either way, so a project that cannot open yet still shows up with what it needs.
    if (registryLoaded) {
        const auto now =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        registry.RecordOpened(projectFile, package.displayName, package.engineVersion, now);
        static_cast<void>(registry.Save(registryFile));
    }
    if (engine == nullptr) {
        return {false, package.displayName + " needs engine " + Pack::FormatProjectVersion(package.engineVersion) +
                           ", which is not downloaded. Download it on the Engines page."};
    }
    const Pack::ProjectCompatibility compatibility =
        Pack::EvaluateProjectCompatibility(package.engineVersion, engine->install.version);
    if (!compatibility.CanOpen()) {
        return {false, compatibility.message};
    }
    Platform::ProcessLaunchDesc launch{
        engine->install.editorExecutable,
        Pack::MakeEditorLaunchArguments(Pack::ProjectRegistry::NormalizeProjectFile(projectFile), registryOverride,
                                  editorZoom),
        projectFile.parent_path()};
#if defined(__ANDROID__)
    g_pendingEditorLaunch = std::move(launch);
    LauncherUI::RequestLauncherClose();
#else
    const Platform::ProcessLaunchResult launched = Platform::LaunchDetachedProcess(launch);
    if (!launched.launched) {
        return {false, launched.message};
    }
#endif
    return {true, "Opening " + package.displayName + " with engine " + engine->install.version.ToString() + "..."};
}

} // namespace UVE::Hub
