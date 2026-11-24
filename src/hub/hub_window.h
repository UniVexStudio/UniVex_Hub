// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

#include "engine_install.h"
#include "engine_library.h"
#include "engine_release_feed.h"
#include "launcher_widgets.h"
#include "project_list.h"
#include "project_registry.h"
#include "project_template.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"

namespace UVE::Hub {

using LauncherUI::NavItem;

inline constexpr std::string_view kHubWindowTitle = "UniVex Hub";
/// The Hub's log, beside the project registry.
inline constexpr std::string_view kHubLogFileName = "uve_hub.log";

/// Where the Hub keeps its state and which engine it launches.
struct HubContext final {
    /// An engine built from source beside the Hub (a development setup), if there is one. An
    /// installed Hub has none; its engines are downloaded into `enginesRoot`.
    std::optional<Pack::EngineInstall> localEngine;
    /// The project templates the Hub ships.
    std::filesystem::path templatesDirectory;
    std::filesystem::path registryFile;
    /// True when `registryFile` was chosen on the command line rather than being the user's own;
    /// the editor is then pointed at the same file.
    bool registryOverridden = false;
    std::filesystem::path settingsFile;
    /// Downloaded engines live here, one folder per version.
    std::filesystem::path enginesRoot;
    /// Where engine releases are published; absent when the Hub ships without a feed.
    std::optional<Online::EngineFeedSource> feed;
    /// True on single-surface mobile screens: replaces the fixed left sidebar with a compact top
    /// navigation bar and stacks wide forms vertically.
    bool compactLayout = false;
};

enum class HubPage : int {
    Projects = 0,
    NewProject,
    Engines,
    Settings,
};

/// The Hub's window contents: the project list, project creation from templates, and the Hub's
/// own settings. Opening or creating a project starts the engine's editor on it.
class HubWindow final {
public:
    explicit HubWindow(HubContext context);
    ~HubWindow();

    /// Builds the widget tree; call once.
    [[nodiscard]] prism::Ref<prism::Widget> Build();
    void ShowPage(HubPage page);
    /// Advances background work (release checks, downloads); call once a frame.
    void Tick();
    /// Shows a message in the status line.
    void ShowMessage(const std::string& message, bool isError) { SetStatus(message, isError); }

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildSidebar();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTopNav();
    [[nodiscard]] prism::Ref<prism::Widget> BuildBrandHeader();
    [[nodiscard]] prism::Ref<prism::Widget> BuildProjectsPage();
    [[nodiscard]] prism::Ref<prism::Widget> BuildNewProjectPage();
    [[nodiscard]] prism::Ref<prism::Widget> BuildSettingsPage();
    [[nodiscard]] prism::Ref<prism::Widget> BuildEnginesPage();
    [[nodiscard]] prism::Ref<prism::Widget> BuildEngineRow(const std::string& title, const std::string& detail,
                                                              prism::Ref<prism::Widget> action);
    [[nodiscard]] prism::Ref<prism::Widget> BuildProjectRow(const Pack::ProjectListRow& row);
    [[nodiscard]] prism::Ref<NavItem> MakeNavItem(const std::string& label, prism::Icon icon, HubPage page);
    /// A path field with a Browse button that opens the folder picker.
    [[nodiscard]] prism::Ref<prism::Widget> FolderField(const prism::Ref<prism::TextInput>& field,
                                                           const std::string& pickerTitle);

    void ReloadRegistry();
    void RefreshInstalledEngines();
    /// The engine versions a new project can be made for: installed ones, then published ones.
    void RefreshNewProjectEngines();
    void RefreshEngineList();
    void CheckForEngines();
    void DownloadEngine(const Online::EngineRelease& release);
    /// Asks for a package file and installs it in the background.
    void ImportEnginePackage();
    /// True while a download or an import is running; only one engine installs at a time.
    [[nodiscard]] bool IsInstallingEngine() const noexcept { return m_download != nullptr || m_import.valid(); }
    [[nodiscard]] bool IsInstalled(const Core::Version& version) const;
    void RefreshProjectList();
    void RefreshTemplateList();
    void SelectTemplate(std::size_t index);
    /// Applies `change` to a freshly loaded registry and saves it, so other tools' updates survive.
    template <class Change> void UpdateRegistry(Change&& change);

    void OpenProject(const std::filesystem::path& projectFile);
    /// Opens the project whose manifest sits in `folder`.
    void OpenProjectFolder(const std::filesystem::path& folder);
    void CreateProject();
    void SaveSettings();
    void SetStatus(const std::string& message, bool isError);
    void StepStartupLoading();

    HubContext m_context;
    Pack::ProjectRegistry m_registry;
    Pack::LauncherSettings m_settings;
    std::vector<Pack::ProjectTemplate> m_templates;
    std::size_t m_selectedTemplate = 0U;
    Pack::ProjectListSort m_sort = Pack::ProjectListSort::RecentlyOpened;

    std::vector<Online::InstalledEngine> m_engines;
    Online::EngineFeedResult m_feedResult;
    std::future<Online::EngineFeedResult> m_feedRequest;
    std::unique_ptr<Online::EngineDownload> m_download;
    std::future<Online::EngineInstallOutcome> m_import;
    /// Where the import picker opens next: the folder of the last imported package.
    std::filesystem::path m_lastImportFolder;

    class HubLoadingOverlay;
    prism::Ref<HubLoadingOverlay> m_loadingOverlay;
    int m_startupStep = 0;
    std::chrono::steady_clock::time_point m_startupBegan{};
    std::chrono::steady_clock::time_point m_startupFadeBegan{};

    prism::Ref<prism::StackSwitcher> m_pages;
    std::vector<std::pair<HubPage, prism::Ref<NavItem>>> m_navigation;
    prism::Ref<prism::Label> m_status;

    prism::Ref<prism::TextInput> m_search;
    prism::Ref<prism::Box> m_projectRows;
    prism::Ref<prism::TextInput> m_openPath;

    prism::Ref<prism::Box> m_templateRows;
    std::vector<prism::Ref<NavItem>> m_templateButtons;
    prism::Ref<prism::Label> m_templateDescription;
    prism::Ref<prism::TextInput> m_projectName;
    prism::Ref<prism::TextInput> m_projectLocation;
    prism::Ref<prism::ComboBox> m_newProjectEngine;
    std::vector<Core::Version> m_newProjectEngineVersions;
    prism::Ref<prism::ComboBox> m_newProjectRenderer;
    prism::Ref<prism::Label> m_rendererDescription;

    prism::Ref<prism::Box> m_engineRows;
    prism::Ref<prism::Label> m_feedStatus;
    prism::Ref<prism::ProgressBar> m_downloadBar;
    prism::Ref<prism::Label> m_downloadLabel;
    prism::Ref<prism::Button> m_importButton;

    prism::Ref<prism::TextInput> m_settingsLocation;
    prism::Ref<prism::TextInput> m_settingsRecentLimit;
};

} // namespace UVE::Hub
