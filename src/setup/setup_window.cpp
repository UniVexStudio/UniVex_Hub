// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "setup_window.h"

#include <array>
#include <chrono>
#include <system_error>
#include <utility>

#include "folder_picker.h"
#include "launcher_files.h"
#include "launcher_window.h"
#include "process_launcher.h"
#include "tar_gz_archive.h"
#include "prism/Theme.h"

namespace UVE::Setup {
namespace {

inline constexpr float kRailWidth = 248.0F;
inline constexpr float kRailPadding = 24.0F;
inline constexpr float kLogoSize = 64.0F;
inline constexpr float kBrandTopSpace = 12.0F;
inline constexpr float kBrandBottomSpace = 16.0F;
inline constexpr float kBrandLineSpacing = 2.0F;
inline constexpr float kPagePadding = 32.0F;
inline constexpr float kSectionSpacing = 14.0F;
inline constexpr float kStepSpacing = 2.0F;
inline constexpr float kControlSpacing = 8.0F;
inline constexpr float kActionBarPadding = 16.0F;
inline constexpr float kListIndent = 12.0F;
/// Share of the bar each stage has reached when it starts; unpacking takes most of the time.
inline constexpr std::array<float, 4U> kStageProgress{0.05F, 0.70F, 0.90F, 1.0F};

/// Each step's name in the rail, per mode; an empty name is a step that mode skips. Steps that are
/// shown are numbered in order.
inline constexpr std::array<std::string_view, kSetupStepCount> kInstallStepNames{"Welcome", "Location", "Install",
                                                                                         "Done"};
inline constexpr std::array<std::string_view, kSetupStepCount> kUninstallStepNames{"Confirm", "", "Remove", "Done"};
/// Space between a step's number and its name.
inline constexpr std::string_view kStepNumberSeparator = "  ";
/// Temporary copy of the payload while it is unpacked.
inline constexpr std::string_view kPayloadScratchPrefix = "univex-hub-setup-";

[[nodiscard]] prism::Ref<prism::Label> Heading(const std::string& text) {
    return prism::make<prism::Label>(text, prism::FontRole::Semibold);
}

[[nodiscard]] prism::Ref<prism::Label> Paragraph(const std::string& text) {
    auto label = prism::make<prism::Label>(text);
    label->setColor(&prism::Theme::textDim);
    return label;
}

[[nodiscard]] std::string StageText(const SetupJob::Stage stage, const SetupMode mode) {
    if (mode == SetupMode::Uninstall) {
        return "Removing UniVex Hub...";
    }
    switch (stage) {
    case SetupJob::Stage::Unpacking:
        return "Unpacking UniVex Hub...";
    case SetupJob::Stage::Installing:
        return "Putting the files in place...";
    case SetupJob::Stage::Registering:
        return "Adding UniVex Hub to your applications...";
    case SetupJob::Stage::Finished:
        return "Finishing...";
    }
    return {};
}

} // namespace

// --- job ----------------------------------------------------------------------------------------

SetupJob::SetupJob(SetupContext context, const bool addToApplications) {
    m_future = std::async(std::launch::async, [this, context = std::move(context), addToApplications]() mutable {
        return Run(std::move(context), addToApplications);
    });
}

bool SetupJob::IsFinished() const {
    return m_result.has_value() || m_future.wait_for(std::chrono::seconds::zero()) == std::future_status::ready;
}

Online::HubSetupResult SetupJob::Result() {
    if (!m_result.has_value()) {
        m_result = m_future.get();
    }
    return *m_result;
}

Online::HubSetupResult SetupJob::Run(SetupContext context, const bool addToApplications) {
    if (context.mode == SetupMode::Uninstall) {
        m_stage.store(Stage::Installing);
        Online::HubSetupResult result = Online::UninstallHub(context.installDirectory, context.entriesRoot);
        m_stage.store(Stage::Finished);
        return result;
    }
    if (!context.payload.has_value()) {
        return {false, "This setup program carries no files to install."};
    }
    const std::filesystem::path archive =
        LauncherUI::UniqueTemporaryPath(kPayloadScratchPrefix, Online::kTarGzFileExtension);
    const auto removeArchive = [&archive] {
        std::error_code ignored;
        std::filesystem::remove(archive, ignored);
    };
    if (archive.empty() || !Online::ExtractSetupPayload(context.setupExecutable, *context.payload, archive)) {
        removeArchive();
        return {false, "This setup program is damaged; download it again."};
    }
    m_stage.store(Stage::Installing);
    Online::HubSetupResult installed =
        Online::InstallHub(archive, context.installDirectory, context.setupExecutable);
    removeArchive();
    if (!installed.succeeded) {
        return installed;
    }
    if (addToApplications) {
        m_stage.store(Stage::Registering);
        const Online::HubSetupResult registered =
            Online::RegisterHubWithDesktop(context.installDirectory, context.version, context.entriesRoot);
        if (!registered.succeeded) {
            installed.message += " " + registered.message;
        }
    }
    m_stage.store(Stage::Finished);
    return installed;
}

// --- window -------------------------------------------------------------------------------------

SetupWindow::SetupWindow(SetupContext context) : m_context(std::move(context)) {
    std::error_code error;
    m_existingInstall = std::filesystem::is_regular_file(Online::HubExecutableIn(m_context.installDirectory), error);
}

prism::Ref<prism::Widget> SetupWindow::Build() {
    m_pages = prism::make<prism::StackSwitcher>();
    m_pages->addPage(BuildWelcomePage());
    m_pages->addPage(BuildLocationPage());
    m_pages->addPage(BuildInstallingPage());
    m_pages->addPage(BuildDonePage());

    auto actions = prism::hbox(kControlSpacing);
    actions->setPadding(prism::Margin(kActionBarPadding));
    m_cancel = prism::make<prism::Button>("Cancel");
    m_cancel->onClick = [] { LauncherUI::RequestLauncherClose(); };
    actions->add(m_cancel);
    actions->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_back = prism::make<prism::Button>("Back");
    m_back->onClick = [this] { ShowStep(SetupStep::Welcome); };
    actions->add(m_back);
    m_next = prism::make<prism::Button>("Next", prism::Icon::None, prism::ButtonStyle::Primary);
    m_next->onClick = [this] {
        if (m_step == SetupStep::Welcome && m_context.mode == SetupMode::Install) {
            ShowStep(SetupStep::Location);
        } else if (m_step == SetupStep::Done) {
            if (m_outcome.has_value() && m_outcome->succeeded && m_context.mode == SetupMode::Install) {
                LaunchHub();
            }
            LauncherUI::RequestLauncherClose();
        } else {
            Start();
        }
    };
    actions->add(m_next);

    auto content = prism::vbox();
    content->add(m_pages, prism::Slot{1.0F});
    content->add(prism::make<prism::Separator>(prism::Orientation::Horizontal));
    content->add(actions);

    auto root = prism::hbox();
    root->add(BuildRail());
    root->add(prism::make<prism::Separator>(prism::Orientation::Vertical));
    root->add(content, prism::Slot{1.0F});
    auto window = prism::make<prism::Border>();
    window->setBackground(&prism::Theme::window).setChild(root);

    ShowStep(SetupStep::Welcome);
    return window;
}

prism::Ref<prism::Widget> SetupWindow::BuildRail() {
    auto rail = prism::vbox(kSectionSpacing);
    rail->setPadding(prism::Margin(kRailPadding));
    // The brand block sits centred in the rail, above the steps.
    rail->add(prism::make<prism::Spacer>(0.0F, kBrandTopSpace));
    rail->add(prism::make<LauncherUI::BrandMark>(kLogoSize), prism::Slot{0.0F, {}, prism::HAlign::Center});
    auto name = prism::vbox(kBrandLineSpacing);
    auto product = prism::make<prism::Label>("UniVex Hub", prism::FontRole::Semibold);
    product->setAlign(prism::HAlign::Center);
    name->add(product, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    auto version = prism::make<prism::Label>("Hub " + m_context.version, prism::FontRole::Small);
    version->setColor(&prism::Theme::textDim);
    version->setAlign(prism::HAlign::Center);
    name->add(version, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    rail->add(name, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    rail->add(prism::make<prism::Spacer>(0.0F, kBrandBottomSpace));

    // The steps are a real sequence, so they are numbered.
    auto steps = prism::vbox(kStepSpacing);
    const auto& names = m_context.mode == SetupMode::Install ? kInstallStepNames : kUninstallStepNames;
    std::size_t number = 0U;
    for (const std::string_view name : names) {
        const std::string label =
            name.empty() ? std::string{} : std::to_string(++number) + std::string(kStepNumberSeparator) + std::string(name);
        auto item = prism::make<LauncherUI::NavItem>(label, prism::Icon::None);
        item->setEnabled(false);
        item->setVisible(!name.empty());
        m_stepItems.push_back(item);
        steps->add(item, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }
    rail->add(steps);

    auto frame = prism::make<prism::Border>();
    frame->setBackground(&prism::Theme::panel).setChild(rail);
    frame->setFixedWidth(kRailWidth);
    return frame;
}

prism::Ref<prism::Widget> SetupWindow::BuildWelcomePage() {
    auto page = prism::vbox(kSectionSpacing);
    page->setPadding(prism::Margin(kPagePadding));
    if (m_context.mode == SetupMode::Uninstall) {
        page->add(Heading("Remove UniVex Hub"));
        page->add(Paragraph("UniVex Hub will be removed from " + m_context.installDirectory.string() + "."));
        page->add(Paragraph("Your projects, your project list and the engines you downloaded stay."));
        return page;
    }
    page->add(Heading(m_existingInstall ? "Update UniVex Hub" : "Install UniVex Hub"));
    page->add(Paragraph("The Hub creates and opens your projects and downloads the engine versions they use."));
    page->add(prism::make<prism::Spacer>(0.0F, kControlSpacing));
    page->add(prism::make<prism::Label>("What is included", prism::FontRole::Medium));
    for (const std::string& item : {"UniVex Hub " + m_context.version, std::string("The project templates"),
                                    std::string("Engines are downloaded from the Hub when you need them")}) {
        auto line = prism::make<prism::Label>(item);
        line->setPadding(prism::Margin(kListIndent, 0.0F, 0.0F, 0.0F));
        page->add(line);
    }
    page->add(prism::make<prism::Spacer>(0.0F, kControlSpacing));
    page->add(Paragraph("It installs for you only; no administrator rights are needed."));
    if (m_existingInstall) {
        page->add(Paragraph("The Hub already installed in " + m_context.installDirectory.string() +
                               " is replaced. Your projects are not touched."));
    }
    if (!m_context.payload.has_value()) {
        auto problem = prism::make<prism::Label>("This setup program carries no files to install; download it again.");
        problem->setColor(&prism::Theme::error);
        page->add(problem);
    }
    return page;
}

prism::Ref<prism::Widget> SetupWindow::BuildLocationPage() {
    auto page = prism::vbox(kSectionSpacing);
    page->setPadding(prism::Margin(kPagePadding));
    page->add(Heading("Where should UniVex Hub go?"));
    page->add(Paragraph("The default folder is yours alone. Projects are saved wherever you choose later."));

    auto row = prism::hbox(kControlSpacing);
    m_location = prism::make<prism::TextInput>(m_context.installDirectory.string());
    m_location->onChanged = [this](const std::string&) { m_locationProblem->setText(""); };
    row->add(m_location, prism::Slot{1.0F});
    auto browse = prism::make<prism::Button>("Browse", prism::Icon::FolderOpen);
    browse->onClick = [this] {
        LauncherUI::ShowFolderPicker("Install UniVex Hub in", std::filesystem::path(m_location->text()).parent_path(),
                                        [this](const std::filesystem::path& chosen) {
                                            m_location->setText((chosen / Online::kHubInstallFolderName).string());
                                        });
    };
    row->add(browse);
    page->add(row);

#if defined(_WIN32)
    m_addToApplications = prism::make<prism::CheckBox>("Add UniVex Hub to the Start menu", true);
#else
    m_addToApplications = prism::make<prism::CheckBox>("Add UniVex Hub to the applications menu", true);
#endif
    page->add(m_addToApplications, prism::Slot{0.0F, {}, prism::HAlign::Left});
    if (m_context.payload.has_value()) {
        page->add(Paragraph("Download size " + LauncherUI::FormatByteSize(m_context.payload->size) +
                               "; the installed files take a few times that."));
    }
    m_locationProblem = prism::make<prism::Label>("", prism::FontRole::Small);
    m_locationProblem->setColor(&prism::Theme::error);
    page->add(m_locationProblem);
    return page;
}

prism::Ref<prism::Widget> SetupWindow::BuildInstallingPage() {
    auto page = prism::vbox(kSectionSpacing);
    page->setPadding(prism::Margin(kPagePadding));
    page->add(Heading(m_context.mode == SetupMode::Install ? "Installing" : "Removing"));
    m_stageLabel = Paragraph("");
    page->add(m_stageLabel);
    m_progress = prism::make<prism::ProgressBar>();
    page->add(m_progress, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    return page;
}

prism::Ref<prism::Widget> SetupWindow::BuildDonePage() {
    auto page = prism::vbox(kSectionSpacing);
    page->setPadding(prism::Margin(kPagePadding));
    m_doneHeading = Heading("");
    page->add(m_doneHeading);
    m_doneMessage = Paragraph("");
    page->add(m_doneMessage);
    return page;
}

void SetupWindow::ShowStep(const SetupStep step) {
    m_step = step;
    m_pages->setActiveIndex(static_cast<int>(step));
    for (std::size_t index = 0U; index < m_stepItems.size(); ++index) {
        m_stepItems[index]->SetActive(static_cast<int>(index) == static_cast<int>(step));
    }
    RefreshActions();
}

void SetupWindow::RefreshActions() {
    const bool install = m_context.mode == SetupMode::Install;
    m_back->setVisible(install && m_step == SetupStep::Location);
    m_cancel->setVisible(m_step == SetupStep::Welcome || m_step == SetupStep::Location);
    m_next->setVisible(m_step != SetupStep::Installing);
    switch (m_step) {
    case SetupStep::Welcome:
        m_next->setText(install ? "Next" : "Remove");
        m_next->setEnabled(!install || m_context.payload.has_value());
        break;
    case SetupStep::Location:
        m_next->setText(m_existingInstall ? "Update" : "Install");
        m_next->setEnabled(true);
        break;
    case SetupStep::Installing:
        break;
    case SetupStep::Done:
        m_next->setText(install && m_outcome.has_value() && m_outcome->succeeded ? "Open UniVex Hub" : "Close");
        m_next->setEnabled(true);
        break;
    }
}

void SetupWindow::Start() {
    bool addToApplications = true;
    if (m_context.mode == SetupMode::Install) {
        const std::filesystem::path chosen = m_location->text();
        if (!chosen.is_absolute() || chosen.filename().empty()) {
            m_locationProblem->setText("Enter a full folder path, for example " +
                                       m_context.installDirectory.string() + ".");
            return;
        }
        m_context.installDirectory = chosen;
        addToApplications = m_addToApplications->checked();
    }
    m_job = std::make_unique<SetupJob>(m_context, addToApplications);
    ShowStep(SetupStep::Installing);
}

void SetupWindow::Tick() {
    if (m_job == nullptr) {
        return;
    }
    const SetupJob::Stage stage = m_job->CurrentStage();
    m_progress->setValue(kStageProgress[static_cast<std::size_t>(stage)]);
    m_stageLabel->setText(StageText(stage, m_context.mode));
    if (!m_job->IsFinished()) {
        return;
    }
    m_outcome = m_job->Result();
    m_job.reset();
    const bool install = m_context.mode == SetupMode::Install;
    if (m_outcome->succeeded) {
        m_doneHeading->setText(install ? "UniVex Hub is ready" : "UniVex Hub was removed");
        m_doneMessage->setText(m_outcome->message);
    } else {
        m_doneHeading->setText(install ? "UniVex Hub was not installed" : "UniVex Hub was not removed");
        m_doneMessage->setText(m_outcome->message);
        m_doneMessage->setColor(&prism::Theme::error);
    }
    ShowStep(SetupStep::Done);
}

void SetupWindow::LaunchHub() {
    static_cast<void>(Platform::LaunchDetachedProcess(
        {Online::HubExecutableIn(m_context.installDirectory), {}, m_context.installDirectory}));
}

} // namespace UVE::Setup
