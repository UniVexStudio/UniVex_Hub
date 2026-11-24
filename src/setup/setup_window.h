// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "hub_setup.h"
#include "launcher_widgets.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"

namespace UVE::Setup {

/// The steps of an installation, in order.
enum class SetupStep : int {
    Welcome = 0,
    Location,
    Installing,
    Done,
};
inline constexpr std::size_t kSetupStepCount = static_cast<std::size_t>(SetupStep::Done) + 1U;

/// What the setup program was started to do.
enum class SetupMode : std::uint8_t {
    Install = 0,
    Uninstall,
};

struct SetupContext final {
    SetupMode mode = SetupMode::Install;
    /// The setup program itself, which carries the payload and becomes the uninstaller.
    std::filesystem::path setupExecutable;
    std::optional<Online::SetupPayloadLocation> payload;
    std::filesystem::path installDirectory;
    /// Where the applications-menu entry goes; empty uses the platform's own place.
    std::filesystem::path entriesRoot;
    std::string version;
};

/// Runs the whole installation off the interface thread: unpack, install, add to the applications
/// menu. Shared by the window and by silent installs.
class SetupJob final {
public:
    enum class Stage : std::uint8_t { Unpacking, Installing, Registering, Finished };

    SetupJob(SetupContext context, bool addToApplications);
    SetupJob(const SetupJob&) = delete;
    SetupJob& operator=(const SetupJob&) = delete;

    [[nodiscard]] Stage CurrentStage() const noexcept { return m_stage.load(); }
    [[nodiscard]] bool IsFinished() const;
    /// Valid once finished.
    [[nodiscard]] Online::HubSetupResult Result();

private:
    [[nodiscard]] Online::HubSetupResult Run(SetupContext context, bool addToApplications);

    std::atomic<Stage> m_stage{Stage::Unpacking};
    std::future<Online::HubSetupResult> m_future;
    std::optional<Online::HubSetupResult> m_result;
};

/// The installer's window: a rail naming the steps beside the page for the current step.
class SetupWindow final {
public:
    explicit SetupWindow(SetupContext context);

    [[nodiscard]] prism::Ref<prism::Widget> Build();
    void ShowStep(SetupStep step);
    void Tick();

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildRail();
    [[nodiscard]] prism::Ref<prism::Widget> BuildWelcomePage();
    [[nodiscard]] prism::Ref<prism::Widget> BuildLocationPage();
    [[nodiscard]] prism::Ref<prism::Widget> BuildInstallingPage();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDonePage();
    void RefreshActions();
    void Start();
    void LaunchHub();

    SetupContext m_context;
    SetupStep m_step = SetupStep::Welcome;
    bool m_existingInstall = false;
    std::unique_ptr<SetupJob> m_job;
    std::optional<Online::HubSetupResult> m_outcome;

    prism::Ref<prism::StackSwitcher> m_pages;
    std::vector<prism::Ref<LauncherUI::NavItem>> m_stepItems;
    prism::Ref<prism::TextInput> m_location;
    prism::Ref<prism::CheckBox> m_addToApplications;
    prism::Ref<prism::Label> m_locationProblem;
    prism::Ref<prism::ProgressBar> m_progress;
    prism::Ref<prism::Label> m_stageLabel;
    prism::Ref<prism::Label> m_doneHeading;
    prism::Ref<prism::Label> m_doneMessage;
    prism::Ref<prism::Button> m_back;
    prism::Ref<prism::Button> m_next;
    prism::Ref<prism::Button> m_cancel;
};

} // namespace UVE::Setup
