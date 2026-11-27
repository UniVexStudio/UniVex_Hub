// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hub_window.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <system_error>
#include <utility>

#include "folder_picker.h"
#include "hub_project_opener.h"
#include "launcher_window.h"
#include "hub_setup.h"
#include "launcher_files.h"
#include "process_launcher.h"
#include "application_runtime.h"
#include "project_compatibility.h"
#include "editor_project_package.h"
#include "prism/Icons.h"
#include "prism/Theme.h"

namespace UVE::Hub {

using LauncherUI::ShowFilePicker;
using LauncherUI::ShowFolderPicker;

namespace {

// Layout, in PRISM layout units.
inline constexpr float kSidebarWidth = 224.0F;
inline constexpr float kCompactSidebarWidth = 136.0F;
inline constexpr float kSidebarInset = 12.0F;
inline constexpr float kCompactSidebarInset = 6.0F;
inline constexpr float kNavSpacing = 2.0F;
inline constexpr float kNavInsetTop = 10.0F;
inline constexpr float kFooterInset = 6.0F;
inline constexpr float kBrandMarkSize = 34.0F;
inline constexpr float kBrandGap = 10.0F;
inline constexpr float kBrandInset = 4.0F;
inline constexpr float kBrandLineSpacing = 1.0F;
inline constexpr float kPagePadding = 22.0F;
inline constexpr float kCompactPagePadding = 10.0F;
inline constexpr float kSectionSpacing = 14.0F;
inline constexpr float kCompactSectionSpacing = 8.0F;
inline constexpr float kCompactBrandMarkSize = 22.0F;
inline constexpr float kRowSpacing = 6.0F;
inline constexpr float kRowPadding = 10.0F;
inline constexpr float kRowRadius = 6.0F;
inline constexpr float kControlSpacing = 6.0F;
inline constexpr float kFieldLabelWidth = 116.0F;
inline constexpr float kTemplateListWidth = 240.0F;
inline constexpr double kHubSplashMinSeconds = 0.80;
inline constexpr double kHubSplashFadeSeconds = 0.25;

[[nodiscard]] float SmoothStep(const float x) noexcept {
    const float t = std::clamp(x, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

struct SortChoice final {
    const char* label;
    Pack::ProjectListSort sort;
};
inline constexpr std::array<SortChoice, 2U> kSortChoices{{
    {"Recently opened", Pack::ProjectListSort::RecentlyOpened},
    {"Name", Pack::ProjectListSort::Name},
}};

[[nodiscard]] prism::ThemeColor StatusColor(const Pack::ProjectListStatus status) {
    switch (status) {
    case Pack::ProjectListStatus::Ready:
        return &prism::Theme::success;
    case Pack::ProjectListStatus::NeedsUpgrade:
    case Pack::ProjectListStatus::OpensWithWarning:
        return &prism::Theme::warning;
    case Pack::ProjectListStatus::EngineNotInstalled:
        return &prism::Theme::textDim;
    case Pack::ProjectListStatus::Incompatible:
    case Pack::ProjectListStatus::Missing:
        return &prism::Theme::error;
    }
    return &prism::Theme::textDim;
}

[[nodiscard]] std::string StatusText(const Pack::ProjectListStatus status) {
    switch (status) {
    case Pack::ProjectListStatus::Ready:
        return "Ready";
    case Pack::ProjectListStatus::NeedsUpgrade:
        return "Asks to upgrade";
    case Pack::ProjectListStatus::OpensWithWarning:
        return "Newer engine";
    case Pack::ProjectListStatus::Incompatible:
        return "Needs a newer engine";
    case Pack::ProjectListStatus::Missing:
        return "Missing";
    case Pack::ProjectListStatus::EngineNotInstalled:
        return "Engine not downloaded";
    }
    return {};
}

[[nodiscard]] prism::Ref<prism::Widget> FieldRow(const std::string& label, prism::Ref<prism::Widget> field,
                                                    const bool stacked = false) {
    if (stacked) {
        auto column = prism::vbox(4.0F);
        auto caption = prism::make<prism::Label>(label, prism::FontRole::Small);
        caption->setColor(&prism::Theme::textDim);
        column->add(caption);
        column->add(std::move(field));
        return column;
    }
    auto row = prism::hbox(kControlSpacing);
    auto caption = prism::make<prism::Label>(label);
    caption->setFixedWidth(kFieldLabelWidth);
    row->add(caption, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(std::move(field), prism::Slot{1.0F});
    return row;
}

[[nodiscard]] prism::Ref<prism::Label> Heading(const std::string& text) {
    return prism::make<prism::Label>(text, prism::FontRole::Semibold);
}

} // namespace

/// Branded UniVex Hub startup splash using the real `univex_logo.png` mark (`prism::drawBrandMark`),
/// version label, live startup step text, sleek progress bar, and smooth fade-out.
class HubWindow::HubLoadingOverlay final : public prism::Widget {
public:
    explicit HubLoadingOverlay(const bool compact)
        : m_compact(compact),
          m_version("v" + Online::FormatDisplayVersion(Online::kHubVersion)) {}

    void SetProgress(std::string step, const float targetProgress, const float alpha) {
        m_step = std::move(step);
        m_targetProgress = std::clamp(targetProgress, 0.0F, 1.0F);
        m_shownProgress += (m_targetProgress - m_shownProgress) * 0.28F;
        m_alpha = std::clamp(alpha, 0.0F, 1.0F);
        if (m_alpha <= 0.005F) {
            if (visible()) {
                setVisible(false);
            }
            return;
        }
        if (!visible()) {
            setVisible(true);
        }
        invalidate();
    }

    prism::Vec2 measure(const prism::Vec2 avail) override {
        return m_desired = constrain(avail);
    }

    bool hitSelf() const override {
        return visible() && m_alpha >= 0.99F;
    }

    void paint(prism::Canvas& c) override {
        if (m_alpha <= 0.005F) {
            return;
        }
        const prism::Theme& t = prism::theme();
        const prism::Rect r = rect();
        const float a = m_alpha;
        const prism::Color accent = t.accent;

        c.gradientV(r, prism::Color(17, 19, 24).alpha(a), prism::Color(11, 12, 16).alpha(a));
        c.fillRect(prism::Rect{r.x, r.y, r.w, 2.0F}, accent.alpha(a * 0.85F));

        const bool compact = m_compact || r.h < 420.0F || r.w < 500.0F;
        const float logoSide = compact ? 72.0F : 96.0F;
        const float barWidth = std::min(300.0F, std::max(168.0F, r.w - 56.0F));
        const float blockHeight = logoSide + (compact ? 104.0F : 122.0F);
        const float topY = std::floor(r.y + (r.h - blockHeight) * 0.5F);
        const float centerX = r.x + r.w * 0.5F;

        const prism::Rect mark{std::floor(centerX - logoSide * 0.5F), topY, logoSide, logoSide};
        if (prism::hasBrandMark()) {
            prism::drawBrandMark(c, mark, a);
        } else {
            c.fillRoundRect(mark, logoSide * 0.22F, accent.alpha(a * 0.22F));
            c.strokeRoundRect(mark, logoSide * 0.22F, accent.alpha(a), 2);
        }

        float y = mark.bottom() + (compact ? 14.0F : 18.0F);
        c.textIn(*t.semibold, prism::Rect{r.x + 24.0F, y, r.w - 48.0F, 24.0F}, "UniVex Hub",
                 t.textBright.alpha(a), prism::HAlign::Center);
        y += 22.0F;
        c.textIn(*t.small, prism::Rect{r.x + 24.0F, y, r.w - 48.0F, 18.0F}, m_version,
                 t.textDim.alpha(a), prism::HAlign::Center);
        y += 24.0F;

        const prism::Rect bar{std::floor(centerX - barWidth * 0.5F), y, barWidth, 5.0F};
        c.fillRoundRect(bar, 2.5F, prism::Color(30, 33, 42).alpha(a));
        c.fillRoundRect(prism::Rect{bar.x, bar.y, std::clamp(bar.w * m_shownProgress, 6.0F, bar.w), bar.h}, 2.5F,
                        accent.alpha(a));
        y += 14.0F;

        const int percent = static_cast<int>(std::round(std::clamp(m_shownProgress, 0.0F, 1.0F) * 100.0F));
        c.textIn(*t.small, prism::Rect{bar.x, y, bar.w - 40.0F, 16.0F}, m_step, t.text.alpha(a), prism::HAlign::Left);
        c.textIn(*t.small, prism::Rect{bar.x, y, bar.w, 16.0F}, std::to_string(percent) + "%",
                 t.textDim.alpha(a), prism::HAlign::Right);
    }

private:
    bool m_compact = false;
    std::string m_version;
    std::string m_step = "Starting UniVex Hub...";
    float m_targetProgress = 0.12F;
    float m_shownProgress = 0.08F;
    float m_alpha = 1.0F;
};

HubWindow::HubWindow(HubContext context) : m_context(std::move(context)) {
    m_settings = Pack::LoadLauncherSettings(m_context.settingsFile);
    LauncherUI::SetLauncherUserScale(m_settings.hubZoom);
}

HubWindow::~HubWindow() = default;

prism::Ref<prism::Widget> HubWindow::Build() {
    const float pagePadding = m_context.compactLayout ? kCompactPagePadding : kPagePadding;
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setPadding(prism::Margin(pagePadding, kRowSpacing));

    m_pages = prism::make<prism::StackSwitcher>();
    m_pages->addPage(BuildProjectsPage());
    m_pages->addPage(BuildNewProjectPage());
    m_pages->addPage(BuildEnginesPage());
    m_pages->addPage(BuildSettingsPage());

    auto content = prism::vbox();
    content->add(m_pages, prism::Slot{1.0F});
    content->add(prism::make<prism::Separator>(prism::Orientation::Horizontal));
    content->add(m_status);

    auto split = prism::hbox();
    split->add(BuildSidebar());
    split->add(prism::make<prism::Separator>(prism::Orientation::Vertical));
    split->add(content, prism::Slot{1.0F});
    prism::Ref<prism::Widget> root = split;

    auto window = prism::make<prism::Border>();
    window->setBackground(&prism::Theme::window).setChild(root);

    ShowPage(HubPage::Projects);

    // Stepwise initialization runs across the first frames in Tick() under the branded loading
    // screen so Frame 1 renders immediately and never appears abruptly half-populated.
    m_startupStep = 0;
    m_startupBegan = std::chrono::steady_clock::now();
    m_startupFadeBegan = {};
    m_loadingOverlay = prism::make<HubLoadingOverlay>(m_context.compactLayout);

    auto overlay = prism::make<prism::Overlay>();
    overlay->add(window);
    overlay->add(m_loadingOverlay);
    return overlay;
}

prism::Ref<NavItem> HubWindow::MakeNavItem(const std::string& label, const prism::Icon icon,
                                                    const HubPage page) {
    auto item = prism::make<NavItem>(label, icon);
    item->SetCompact(m_context.compactLayout);
    item->onActivate = [this, page] { ShowPage(page); };
    m_navigation.emplace_back(page, item);
    return item;
}

prism::Ref<prism::Widget> HubWindow::BuildBrandHeader() {
    const bool compact = m_context.compactLayout;
    auto names = prism::vbox(kBrandLineSpacing);
    names->add(prism::make<prism::Label>("UniVex Hub", prism::FontRole::Semibold));
    auto version = prism::make<prism::Label>(
        (compact ? "v" : "Hub ") + Online::FormatDisplayVersion(Online::kHubVersion), prism::FontRole::Small);
    version->setColor(&prism::Theme::textDim);
    names->add(version);

    auto header = prism::hbox(compact ? kRowSpacing : kBrandGap);
    const float inset = compact ? kCompactSidebarInset : kSidebarInset;
    header->setPadding(prism::Margin(inset + kBrandInset, compact ? kCompactSectionSpacing : kSectionSpacing));
    header->add(prism::make<LauncherUI::BrandMark>(compact ? kCompactBrandMarkSize : kBrandMarkSize),
                prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    header->add(names, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    return header;
}

prism::Ref<prism::Widget> HubWindow::BuildSidebar() {
    const bool compact = m_context.compactLayout;
    const float inset = compact ? kCompactSidebarInset : kSidebarInset;
    auto navigation = prism::vbox(kNavSpacing);
    navigation->setPadding(prism::Margin(inset, compact ? kRowSpacing : kNavInsetTop, inset, 0.0F));
    navigation->add(MakeNavItem("Projects", prism::Icon::Folder, HubPage::Projects),
                    prism::Slot{0.0F, {}, prism::HAlign::Fill});
    navigation->add(MakeNavItem("New Project", prism::Icon::Plus, HubPage::NewProject),
                    prism::Slot{0.0F, {}, prism::HAlign::Fill});
    navigation->add(MakeNavItem("Engines", prism::Icon::Layers, HubPage::Engines),
                    prism::Slot{0.0F, {}, prism::HAlign::Fill});

    // Settings is about the Hub itself, not a place to work in, so it sits apart at the bottom.
    auto footer = prism::vbox(kNavSpacing);
    footer->setPadding(prism::Margin(inset, kFooterInset));
    footer->add(MakeNavItem("Settings", prism::Icon::Settings, HubPage::Settings),
                prism::Slot{0.0F, {}, prism::HAlign::Fill});

    auto sidebar = prism::vbox();
    sidebar->add(BuildBrandHeader());
    sidebar->add(prism::make<prism::Separator>(prism::Orientation::Horizontal));
    sidebar->add(navigation);
    sidebar->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    sidebar->add(prism::make<prism::Separator>(prism::Orientation::Horizontal));
    sidebar->add(footer);

    auto frame = prism::make<prism::Border>();
    frame->setBackground(&prism::Theme::panel).setChild(sidebar);
    frame->setFixedWidth(compact ? kCompactSidebarWidth : kSidebarWidth);
    return frame;
}

prism::Ref<prism::Widget> HubWindow::BuildTopNav() {
    auto brand = prism::hbox(kRowSpacing);
    brand->setPadding(prism::Margin(kCompactPagePadding, 8.0F, kCompactPagePadding, 4.0F));
    brand->add(prism::make<LauncherUI::BrandMark>(kCompactBrandMarkSize),
               prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    brand->add(prism::make<prism::Label>("UniVex Hub", prism::FontRole::Semibold),
               prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto version = prism::make<prism::Label>("v" + Online::FormatDisplayVersion(Online::kHubVersion),
                                             prism::FontRole::Small);
    version->setColor(&prism::Theme::textDim);
    brand->add(version, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});

    auto tabs = prism::hbox(4.0F);
    tabs->setPadding(prism::Margin(8.0F, 2.0F, 8.0F, 6.0F));
    tabs->add(MakeNavItem("Projects", prism::Icon::Folder, HubPage::Projects), prism::Slot{1.0F});
    tabs->add(MakeNavItem("New", prism::Icon::Plus, HubPage::NewProject), prism::Slot{1.0F});
    tabs->add(MakeNavItem("Engines", prism::Icon::Layers, HubPage::Engines), prism::Slot{1.0F});
    tabs->add(MakeNavItem("Settings", prism::Icon::Settings, HubPage::Settings), prism::Slot{1.0F});

    auto bar = prism::vbox();
    bar->add(brand);
    bar->add(tabs);

    auto frame = prism::make<prism::Border>();
    frame->setBackground(&prism::Theme::panel).setChild(bar);
    return frame;
}

prism::Ref<prism::Widget> HubWindow::FolderField(const prism::Ref<prism::TextInput>& field,
                                                       const std::string& pickerTitle) {
    auto row = prism::hbox(kControlSpacing);
    row->add(field, prism::Slot{1.0F});
    auto browse = prism::make<prism::Button>("Browse", prism::Icon::FolderOpen);
    browse->onClick = [field, pickerTitle] {
        ShowFolderPicker(pickerTitle, field->text(),
                            [field](const std::filesystem::path& chosen) { field->setText(chosen.string(), true); });
    };
    row->add(browse);
    return row;
}

prism::Ref<prism::Widget> HubWindow::BuildProjectsPage() {
    const float pagePadding = m_context.compactLayout ? kCompactPagePadding : kPagePadding;
    const float sectionSpacing = m_context.compactLayout ? kCompactSectionSpacing : kSectionSpacing;
    auto page = prism::vbox(sectionSpacing);
    page->setPadding(prism::Margin(pagePadding));

    auto header = prism::hbox(kControlSpacing);
    header->add(Heading("Projects"), prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    std::vector<std::string> sortLabels;
    for (const SortChoice& choice : kSortChoices) {
        sortLabels.emplace_back(choice.label);
    }
    auto sort = prism::make<prism::ComboBox>(sortLabels, 0);
    sort->onChange = [this](const int index) {
        m_sort = kSortChoices[static_cast<std::size_t>(index)].sort;
        RefreshProjectList();
    };
    header->add(sort);
    auto refresh =
        prism::make<prism::Button>(m_context.compactLayout ? "" : "Refresh", prism::Icon::Refresh);
    refresh->setTooltip("Refresh projects");
    refresh->onClick = [this] { ReloadRegistry(); };
    header->add(refresh);
    page->add(header);

    m_search = prism::make<prism::TextInput>("", "Search projects");
    m_search->setLeadingIcon(prism::Icon::Search);
    m_search->setClearable(true);
    m_search->onChanged = [this](const std::string&) { RefreshProjectList(); };
    page->add(m_search);

    auto openRow = prism::hbox(kControlSpacing);
    m_openPath = prism::make<prism::TextInput>("", "Path to a .uvproject file");
    m_openPath->onCommit = [this](const std::string& path) { OpenProject(path); };
    openRow->add(m_openPath, prism::Slot{1.0F});
    auto open = prism::make<prism::Button>("Open", prism::Icon::Open);
    open->onClick = [this] { OpenProject(m_openPath->text()); };
    openRow->add(open);
    auto browse = prism::make<prism::Button>(m_context.compactLayout ? "Folder" : "Open project folder",
                                             prism::Icon::FolderOpen);
    browse->setTooltip("Browse for a project folder or .uvproject file");
    browse->onClick = [this] {
        LauncherUI::ShowProjectPicker("Open a project or folder", m_settings.defaultProjectLocation,
                                      [this](const std::filesystem::path& target) { OpenProjectFolder(target); });
    };
    openRow->add(browse);
    page->add(openRow);

    m_projectRows = prism::vbox(kRowSpacing);
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_projectRows);
    page->add(scroll, prism::Slot{1.0F});
    return page;
}

prism::Ref<prism::Widget> HubWindow::BuildNewProjectPage() {
    const float pagePadding = m_context.compactLayout ? kCompactPagePadding : kPagePadding;
    const float sectionSpacing = m_context.compactLayout ? kCompactSectionSpacing : kSectionSpacing;
    auto page = prism::vbox(sectionSpacing);
    page->setPadding(prism::Margin(pagePadding));
    page->add(Heading("New Project"));

    m_templateRows = prism::vbox(kRowSpacing);
    m_templateDescription = prism::make<prism::Label>();
    m_templateDescription->setColor(&prism::Theme::textDim);
    m_projectName = prism::make<prism::TextInput>("", "My Game");
    m_projectLocation = prism::make<prism::TextInput>(m_settings.defaultProjectLocation.string());
    m_newProjectEngine = prism::make<prism::ComboBox>();
    const Platform::RendererProfile defaultProfile =
        m_context.compactLayout ? Platform::RendererProfile::Compatibility : Platform::DefaultRendererProfileForHost();
    const std::vector<std::string> rendererLabels{
        std::string(Platform::GetRendererProfileDisplayName(Platform::RendererProfile::Compatibility)),
        std::string(Platform::GetRendererProfileDisplayName(Platform::RendererProfile::Mobile)),
        std::string(Platform::GetRendererProfileDisplayName(Platform::RendererProfile::ForwardPlus)),
    };
    m_newProjectRenderer = prism::make<prism::ComboBox>(rendererLabels, static_cast<int>(defaultProfile));
    m_rendererDescription = prism::make<prism::Label>(
        std::string(Platform::GetRendererProfileDescription(defaultProfile)), prism::FontRole::Small);
    m_rendererDescription->setColor(&prism::Theme::textDim);
    m_newProjectRenderer->onChange = [this](const int index) {
        const auto profile = static_cast<Platform::RendererProfile>(std::clamp(index, 0, 2));
        m_rendererDescription->setText(std::string(Platform::GetRendererProfileDescription(profile)));
    };
    auto create = prism::make<prism::Button>("Create and Open", prism::Icon::Plus, prism::ButtonStyle::Primary);
    create->onClick = [this] { CreateProject(); };

    if (m_context.compactLayout) {
        auto stacked = prism::vbox(sectionSpacing);
        stacked->add(m_templateRows);
        stacked->add(m_templateDescription);
        stacked->add(FieldRow("Name", m_projectName, true));
        stacked->add(FieldRow("Location", FolderField(m_projectLocation, "Where to create the project"), true));
        stacked->add(FieldRow("Engine", m_newProjectEngine, true));
        stacked->add(FieldRow("Renderer", m_newProjectRenderer, true));
        stacked->add(m_rendererDescription);
        stacked->add(create, prism::Slot{0.0F, {}, prism::HAlign::Fill});
        auto scroll = prism::make<prism::ScrollView>();
        scroll->setContent(stacked);
        page->add(scroll, prism::Slot{1.0F});
        return page;
    }

    auto body = prism::hbox(kSectionSpacing);
    auto templates = prism::make<prism::ScrollView>();
    templates->setContent(m_templateRows);
    templates->setFixedWidth(kTemplateListWidth);
    body->add(templates);

    auto details = prism::vbox(kSectionSpacing);
    details->add(m_templateDescription);
    details->add(FieldRow("Name", m_projectName));
    details->add(FieldRow("Location", FolderField(m_projectLocation, "Where to create the project")));
    details->add(FieldRow("Engine", m_newProjectEngine));
    details->add(FieldRow("Renderer", m_newProjectRenderer));
    details->add(m_rendererDescription);
    details->add(create, prism::Slot{0.0F, {}, prism::HAlign::Left});
    auto detailsScroll = prism::make<prism::ScrollView>();
    detailsScroll->setContent(details);
    body->add(detailsScroll, prism::Slot{1.0F});

    page->add(body, prism::Slot{1.0F});
    return page;
}

prism::Ref<prism::Widget> HubWindow::BuildSettingsPage() {
    const float pagePadding = m_context.compactLayout ? kCompactPagePadding : kPagePadding;
    const float sectionSpacing = m_context.compactLayout ? kCompactSectionSpacing : kSectionSpacing;
    auto page = prism::vbox(sectionSpacing);
    page->setPadding(prism::Margin(pagePadding));
    page->add(Heading("Settings"));
    m_settingsLocation = prism::make<prism::TextInput>(m_settings.defaultProjectLocation.string());
    m_settingsRecentLimit = prism::make<prism::TextInput>();
    auto hint = prism::make<prism::Label>("Favorites are always kept. Between " +
                                              std::to_string(Pack::kMinimumRecentProjectLimit) + " and " +
                                              std::to_string(Pack::kMaximumRecentProjectLimit) + ".",
                                          prism::FontRole::Small);
    hint->setColor(&prism::Theme::textDim);
    auto save = prism::make<prism::Button>("Save", prism::Icon::Save, prism::ButtonStyle::Primary);
    save->onClick = [this] { SaveSettings(); };

    // Zoom presets: the Hub's own zoom applies at once; the editor's is handed to it on launch.
    constexpr std::array<float, 7> kZoomPresets{0.65F, 0.75F, 0.90F, 1.00F, 1.15F, 1.25F, 1.50F};
    const auto zoomRow = [this, &kZoomPresets](const char* label, const bool hub) {
        auto presets = prism::make<prism::Segmented>();
        int selected = 3;
        float nearest = 10.0F;
        const float current = hub ? m_settings.hubZoom : m_settings.editorZoom;
        for (std::size_t index = 0U; index < kZoomPresets.size(); ++index) {
            presets->add(std::to_string(static_cast<int>(std::lround(kZoomPresets[index] * 100.0F))) + "%");
            if (std::abs(kZoomPresets[index] - current) < nearest) {
                nearest = std::abs(kZoomPresets[index] - current);
                selected = static_cast<int>(index);
            }
        }
        presets->setSelected(selected);
        presets->onSelect = [this, hub, kZoomPresets](const int index) {
            if (index < 0 || index >= static_cast<int>(kZoomPresets.size())) {
                return;
            }
            (hub ? m_settings.hubZoom : m_settings.editorZoom) = kZoomPresets[static_cast<std::size_t>(index)];
            if (hub) {
                LauncherUI::SetLauncherUserScale(m_settings.hubZoom);
            }
            static_cast<void>(Pack::SaveLauncherSettings(m_context.settingsFile, m_settings));
        };
        return FieldRow(label, presets, m_context.compactLayout);
    };

    if (m_context.compactLayout) {
        auto fields = prism::vbox(sectionSpacing);
        fields->add(FieldRow("New projects in", FolderField(m_settingsLocation, "Where new projects go"), true));
        fields->add(FieldRow("Recent projects", m_settingsRecentLimit, true));
        fields->add(hint);
        fields->add(zoomRow("Hub zoom", true));
        fields->add(zoomRow("Editor zoom", false));
        fields->add(save, prism::Slot{0.0F, {}, prism::HAlign::Left});
        auto scroll = prism::make<prism::ScrollView>();
        scroll->setContent(fields);
        page->add(scroll, prism::Slot{1.0F});
        return page;
    }

    auto fields = prism::vbox(sectionSpacing);
    fields->add(FieldRow("New projects in", FolderField(m_settingsLocation, "Where new projects go")));
    fields->add(FieldRow("Recent projects", m_settingsRecentLimit));
    fields->add(hint);
    fields->add(zoomRow("Hub zoom", true));
    fields->add(zoomRow("Editor zoom", false));
    fields->add(save, prism::Slot{0.0F, {}, prism::HAlign::Left});
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(fields);
    page->add(scroll, prism::Slot{1.0F});

    return page;
}

void HubWindow::ShowPage(const HubPage page) {
    m_pages->setActiveIndex(static_cast<int>(page));
    for (const auto& [itemPage, item] : m_navigation) {
        item->SetActive(itemPage == page);
    }
}

template <class Change> void HubWindow::UpdateRegistry(Change&& change) {
    const Pack::ProjectRegistryResult loaded = m_registry.Load(m_context.registryFile);
    if (!loaded.IsApplied()) {
        SetStatus(loaded.message, true);
        return;
    }
    change(m_registry);
    const Pack::ProjectRegistryResult saved = m_registry.Save(m_context.registryFile);
    if (!saved.IsApplied()) {
        SetStatus(saved.message, true);
    }
    RefreshProjectList();
}

void HubWindow::ReloadRegistry() {
    const Pack::ProjectRegistryResult loaded = m_registry.Load(m_context.registryFile);
    if (!loaded.IsApplied()) {
        SetStatus(loaded.message, true);
    } else {
        // Sync each existing project's manifest version/name so Hub never shows a stale Upgrade badge.
        bool syncedAny = false;
        const std::vector<Pack::ProjectRegistryEntry> snapshot = m_registry.Entries();
        for (const Pack::ProjectRegistryEntry& entry : snapshot) {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(entry.projectFile, ec)) {
                continue;
            }
            const Platform::EditorProjectPackageLoadResult manifest =
                Platform::EditorProjectPackageCodec::Load(entry.projectFile);
            if (manifest.IsAccepted() && manifest.package.has_value() &&
                (manifest.package->engineVersion != entry.engineVersion ||
                 manifest.package->displayName != entry.displayName)) {
                m_registry.RecordOpened(entry.projectFile, manifest.package->displayName,
                                        manifest.package->engineVersion, entry.lastOpenedUnixSeconds);
                syncedAny = true;
            }
        }
        if (syncedAny) {
            static_cast<void>(m_registry.Save(m_context.registryFile));
        }
    }
    m_settingsRecentLimit->setText(std::to_string(m_registry.RecentLimit()));
    RefreshProjectList();
}

void HubWindow::RefreshProjectList() {
    m_projectRows->clearChildren();
    const std::vector<Pack::ProjectListRow> rows =
        Pack::BuildProjectList(m_registry.Entries(), m_search->text(), m_sort, Core::Version{});
    if (rows.empty()) {
        auto empty = prism::make<prism::Label>(m_registry.Entries().empty()
                                                   ? "No projects yet. Create one, or open a .uvproject file."
                                                   : "No project matches the search.");
        empty->setColor(&prism::Theme::textDim);
        m_projectRows->add(empty);
    }
    for (Pack::ProjectListRow row : rows) {
        // The badge describes the engine that would actually open the project.
        if (row.status != Pack::ProjectListStatus::Missing) {
            const Online::InstalledEngine* const engine =
                Online::ChooseEngineForProject(m_engines, row.entry.engineVersion, row.entry.engineChoice);
            row.status = engine == nullptr
                             ? Pack::ProjectListStatus::EngineNotInstalled
                             : Pack::ProjectListStatusFor(row.entry.engineVersion, engine->install.version);
        }
        m_projectRows->add(BuildProjectRow(row));
    }
    m_projectRows->relayout();
}

prism::Ref<prism::Widget> HubWindow::BuildProjectRow(const Pack::ProjectListRow& row) {
    const std::filesystem::path projectFile = row.entry.projectFile;
    auto text = prism::vbox();
    text->add(prism::make<prism::Label>(row.entry.displayName, prism::FontRole::Semibold));
    auto path = prism::make<prism::Label>(projectFile.parent_path().string(), prism::FontRole::Small);
    path->setColor(&prism::Theme::textDim);
    text->add(path);
    auto badge = prism::make<prism::Label>(row.badge + " - " + StatusText(row.status), prism::FontRole::Small);
    badge->setColor(StatusColor(row.status));
    text->add(badge);

    const bool openable = row.status != Pack::ProjectListStatus::Missing &&
                          row.status != Pack::ProjectListStatus::Incompatible &&
                          row.status != Pack::ProjectListStatus::EngineNotInstalled;
    prism::Ref<prism::Widget> primaryAction;
    if (row.status == Pack::ProjectListStatus::EngineNotInstalled) {
        auto getEngine = prism::make<prism::Button>("Get engine", prism::Icon::Open);
        getEngine->setTooltip("Download engine " + Pack::FormatProjectVersion(row.entry.engineVersion) +
                              " on the Engines page");
        getEngine->onClick = [this] { ShowPage(HubPage::Engines); };
        primaryAction = getEngine;
    } else if (openable) {
        auto open = prism::make<prism::Button>("Open", prism::Icon::Play, prism::ButtonStyle::Primary);
        open->onClick = [this, projectFile] { OpenProject(projectFile); };
        primaryAction = open;
    }

    prism::Ref<prism::Widget> engineChoice;
    if (row.status != Pack::ProjectListStatus::Missing) {
        // "Automatic" lets the Hub pick; otherwise the exact installed version the user chose.
        std::vector<std::string> choices{"Automatic"};
        int selected = 0;
        for (const Online::InstalledEngine& engine : m_engines) {
            choices.push_back("Engine " + engine.install.version.ToString());
            if (engine.install.version.ToString() == row.entry.engineChoice) {
                selected = static_cast<int>(choices.size()) - 1;
            }
        }
        auto engine = prism::make<prism::ComboBox>(choices, selected);
        engine->setTooltip("Engine used to open this project");
        engine->onChange = [this, projectFile](const int index) {
            const std::string choice =
                index <= 0 ? std::string{}
                           : m_engines[static_cast<std::size_t>(index - 1)].install.version.ToString();
            UpdateRegistry([&](Pack::ProjectRegistry& registry) {
                static_cast<void>(registry.SetEngineChoice(projectFile, choice));
            });
        };
        engineChoice = engine;
    }

    const std::string favoriteLabel =
        m_context.compactLayout ? (row.entry.favorite ? "Starred" : "Star")
                                : (row.entry.favorite ? "Favorite" : "Add to favorites");
    auto favorite = prism::make<prism::Button>(favoriteLabel, prism::Icon::Check, prism::ButtonStyle::Flat);
    favorite->setCheckable(true);
    favorite->setChecked(row.entry.favorite);
    favorite->onClick = [this, projectFile, wasFavorite = row.entry.favorite] {
        UpdateRegistry([&](Pack::ProjectRegistry& registry) {
            static_cast<void>(registry.SetFavorite(projectFile, !wasFavorite));
        });
    };

    auto remove = prism::make<prism::Button>("", prism::Icon::Trash, prism::ButtonStyle::Flat);
    remove->setTooltip("Remove from the list (the project stays on disk)");
    remove->onClick = [this, projectFile] {
        UpdateRegistry([&](Pack::ProjectRegistry& registry) { static_cast<void>(registry.Remove(projectFile)); });
    };

    prism::Ref<prism::Widget> content;
    if (m_context.compactLayout) {
        auto top = prism::hbox(kControlSpacing);
        top->add(text, prism::Slot{1.0F});
        if (primaryAction != nullptr) {
            top->add(primaryAction, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        }
        auto controls = prism::hbox(kControlSpacing);
        if (engineChoice != nullptr) {
            controls->add(engineChoice, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        } else {
            controls->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
        }
        controls->add(favorite, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        controls->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});

        auto stacked = prism::vbox(kRowSpacing);
        stacked->add(top);
        stacked->add(controls);
        content = stacked;
    } else {
        auto line = prism::hbox(kControlSpacing);
        line->add(text, prism::Slot{1.0F});
        if (primaryAction != nullptr) {
            line->add(primaryAction, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        }
        if (engineChoice != nullptr) {
            line->add(engineChoice, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        }
        line->add(favorite, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
#if !defined(__ANDROID__)
        if (row.status != Pack::ProjectListStatus::Missing) {
            auto reveal = prism::make<prism::Button>("Show in folder", prism::Icon::FolderOpen, prism::ButtonStyle::Flat);
            reveal->onClick = [this, projectFile] {
                const Platform::ProcessLaunchResult revealed =
                    Platform::RevealInFileManager(projectFile.parent_path());
                if (!revealed.launched) {
                    SetStatus(revealed.message, true);
                }
            };
            line->add(reveal, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        }
#endif
        line->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        content = line;
    }

    auto card = prism::make<prism::Border>();
    card->setBackground(&prism::Theme::panel)
        .setBorder(&prism::Theme::borderSoft)
        .setRadius(kRowRadius)
        .setPadding(prism::Margin(kRowPadding))
        .setChild(content);
    return card;
}

void HubWindow::RefreshTemplateList() {
    const Pack::ProjectTemplateDiscovery discovery =
        Pack::DiscoverProjectTemplates(m_context.templatesDirectory);
    m_templates = discovery.templates;
    m_templateRows->clearChildren();
    m_templateButtons.clear();
    for (std::size_t index = 0U; index < m_templates.size(); ++index) {
        auto button = prism::make<NavItem>(m_templates[index].displayName, prism::Icon::Cube);
        button->onActivate = [this, index] { SelectTemplate(index); };
        m_templateButtons.push_back(button);
        m_templateRows->add(button, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }
    if (m_templates.empty()) {
        m_templateDescription->setText("No project templates were found in " +
                                       m_context.templatesDirectory.string() + ".");
    } else {
        SelectTemplate(0U);
    }
    if (!discovery.problems.empty()) {
        SetStatus(discovery.problems.front(), true);
    }
}

void HubWindow::SelectTemplate(const std::size_t index) {
    if (index >= m_templates.size()) {
        return;
    }
    m_selectedTemplate = index;
    for (std::size_t button = 0U; button < m_templateButtons.size(); ++button) {
        m_templateButtons[button]->SetActive(button == index);
    }
    m_templateDescription->setText(m_templates[index].description);
}

void HubWindow::OpenProject(const std::filesystem::path& projectFile) {
    std::error_code error;
    if (!projectFile.empty() && std::filesystem::is_directory(projectFile, error)) {
        OpenProjectFolder(projectFile);
        return;
    }
    const ProjectOpenResult opened = OpenProjectInEngine(
        projectFile, m_engines, m_context.registryFile,
        m_context.registryOverridden ? m_context.registryFile : std::filesystem::path{}, m_settings.editorZoom);
    ReloadRegistry();
    SetStatus(opened.message, !opened.launched);
}

void HubWindow::OpenProjectFolder(const std::filesystem::path& folder) {
    bool storageRestricted = false;
#if defined(__ANDROID__)
    if (!LauncherUI::HasStorageAccess()) {
        const std::string norm = folder.lexically_normal().generic_string();
        const char* const appExt = std::getenv("UNIVEX_ANDROID_APP_EXT_DOCS");
        const bool inAppExt = appExt != nullptr && appExt[0] != '\0' && norm.starts_with(appExt);
        const Platform::ApplicationUserDataDirectoryResult home = Platform::ResolveUserHomeDirectory();
        const bool inAppPrivate = home.success && norm.starts_with(home.directory.lexically_normal().generic_string());
        storageRestricted = !inAppExt && !inAppPrivate;
    }
#endif
    const Pack::ProjectFolderLookupResult lookup = Pack::FindProjectManifestInFolder(folder, storageRestricted);
    if (!lookup.projectFile.has_value()) {
        SetStatus(lookup.message, true);
        return;
    }
    OpenProject(*lookup.projectFile);
}

void HubWindow::CreateProject() {
    if (m_selectedTemplate >= m_templates.size()) {
        SetStatus("Choose a template first.", true);
        return;
    }
    const std::filesystem::path location = m_projectLocation->text();
    std::error_code error;
    if (location.is_absolute()) {
        std::filesystem::create_directories(location, error);
    }
    const int engineIndex = m_newProjectEngine->selected();
    if (engineIndex < 0 || static_cast<std::size_t>(engineIndex) >= m_newProjectEngineVersions.size()) {
        SetStatus("No engine version is known yet. Go online once, or download an engine on the Engines page.",
                     true);
        return;
    }
    const Core::Version engineVersion = m_newProjectEngineVersions[static_cast<std::size_t>(engineIndex)];
    const int rendererIndex = m_newProjectRenderer ? std::clamp(m_newProjectRenderer->selected(), 0, 2)
                                                   : static_cast<int>(Platform::DefaultRendererProfileForHost());
    const auto rendererProfile = static_cast<Platform::RendererProfile>(rendererIndex);
    const std::string displayName = m_projectName->text();
    const Pack::ProjectCreationResult created = Pack::CreateProjectFromTemplate(
        m_templates[m_selectedTemplate], location, m_projectName->text(), engineVersion, rendererProfile);
    if (!created.IsCreated()) {
        SetStatus(created.message, true);
        return;
    }
    m_projectName->setText("");
    ShowPage(HubPage::Projects);
    if (IsInstalled(engineVersion)) {
        OpenProject(created.projectFile);
        return;
    }
    // The project is ready; it opens once its engine is downloaded.
    UpdateRegistry([&](Pack::ProjectRegistry& registry) {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        registry.RecordOpened(created.projectFile, displayName,
                                 {engineVersion.major, engineVersion.minor, engineVersion.patch, engineVersion.build}, now);
    });
    SetStatus("Project created. Download engine " + engineVersion.ToString() +
                     " on the Engines page to open it.",
                 false);
}

void HubWindow::SaveSettings() {
    const std::filesystem::path location = m_settingsLocation->text();
    if (!location.is_absolute()) {
        SetStatus("The project folder must be a full path.", true);
        return;
    }
    const std::string& limitText = m_settingsRecentLimit->text();
    std::size_t limit = 0U;
    const auto [end, parseError] = std::from_chars(limitText.data(), limitText.data() + limitText.size(), limit);
    if (parseError != std::errc{} || end != limitText.data() + limitText.size()) {
        SetStatus("The number of recent projects must be a whole number.", true);
        return;
    }
    m_settings.defaultProjectLocation = location;
    if (!Pack::SaveLauncherSettings(m_context.settingsFile, m_settings)) {
        SetStatus("Unable to save the Hub settings to " + m_context.settingsFile.string() + ".", true);
        return;
    }
    m_projectLocation->setText(location.string());
    UpdateRegistry([limit](Pack::ProjectRegistry& registry) { registry.SetRecentLimit(limit); });
    m_settingsRecentLimit->setText(std::to_string(m_registry.RecentLimit()));
    SetStatus("Settings saved.", false);
}

void HubWindow::SetStatus(const std::string& message, const bool isError) {
    m_status->setText(message);
    m_status->setColor(isError ? prism::ThemeColor(&prism::Theme::error) : prism::ThemeColor(&prism::Theme::textDim));
}

prism::Ref<prism::Widget> HubWindow::BuildEnginesPage() {
    const float pagePadding = m_context.compactLayout ? kCompactPagePadding : kPagePadding;
    const float sectionSpacing = m_context.compactLayout ? kCompactSectionSpacing : kSectionSpacing;
    auto page = prism::vbox(sectionSpacing);
    page->setPadding(prism::Margin(pagePadding));

    auto header = prism::hbox(kControlSpacing);
    header->add(Heading("Engines"), prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto check = prism::make<prism::Button>(m_context.compactLayout ? "Check" : "Check for new versions",
                                            prism::Icon::Refresh);
    check->setTooltip("Check for new engine versions");
    check->onClick = [this] { CheckForEngines(); };
    header->add(check);
    m_importButton =
        prism::make<prism::Button>(m_context.compactLayout ? "Import..." : "Import package...", prism::Icon::Open);
    m_importButton->setTooltip("Install an engine package file you already have, such as an offline copy");
    m_importButton->onClick = [this] { ImportEnginePackage(); };
    header->add(m_importButton);
    page->add(header);

    m_feedStatus = prism::make<prism::Label>("", prism::FontRole::Small);
    m_feedStatus->setColor(&prism::Theme::textDim);
    page->add(m_feedStatus);

    auto progress = prism::hbox(kControlSpacing);
    m_downloadLabel = prism::make<prism::Label>("", prism::FontRole::Small);
    progress->add(m_downloadLabel, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_downloadBar = prism::make<prism::ProgressBar>();
    progress->add(m_downloadBar, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    progress->setVisible(false);
    page->add(progress);

    m_engineRows = prism::vbox(kRowSpacing);
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_engineRows);
    page->add(scroll, prism::Slot{1.0F});
    return page;
}

prism::Ref<prism::Widget> HubWindow::BuildEngineRow(const std::string& title, const std::string& detail,
                                                          prism::Ref<prism::Widget> action) {
    auto text = prism::vbox();
    text->add(prism::make<prism::Label>(title, prism::FontRole::Semibold));
    auto detailLabel = prism::make<prism::Label>(detail, prism::FontRole::Small);
    detailLabel->setColor(&prism::Theme::textDim);
    text->add(detailLabel);
    auto line = prism::hbox(kControlSpacing);
    line->add(text, prism::Slot{1.0F});
    if (action != nullptr) {
        line->add(std::move(action), prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    }
    auto card = prism::make<prism::Border>();
    card->setBackground(&prism::Theme::panel)
        .setBorder(&prism::Theme::borderSoft)
        .setRadius(kRowRadius)
        .setPadding(prism::Margin(kRowPadding))
        .setChild(line);
    return card;
}

void HubWindow::RefreshInstalledEngines() {
    std::optional<Online::InstalledEngine> local;
    if (m_context.localEngine.has_value()) {
        local = Online::InstalledEngine{*m_context.localEngine, Online::EngineChannel::Stable, true};
    }
    m_engines = Online::ListInstalledEngines(m_context.enginesRoot, local);
}

bool HubWindow::IsInstalled(const Core::Version& version) const {
    return std::any_of(m_engines.begin(), m_engines.end(),
                       [&version](const Online::InstalledEngine& engine) { return engine.install.version == version; });
}

void HubWindow::RefreshNewProjectEngines() {
    std::vector<std::string> labels;
    m_newProjectEngineVersions.clear();
    for (const Online::InstalledEngine& engine : m_engines) {
        m_newProjectEngineVersions.push_back(engine.install.version);
        labels.push_back("Engine " + engine.install.version.ToString() + " (installed)");
    }
    for (const Online::EngineRelease& release : m_feedResult.releases) {
        if (IsInstalled(release.version)) {
            continue;
        }
        m_newProjectEngineVersions.push_back(release.version);
        labels.push_back("Engine " + release.version.ToString() + " (" +
                         std::string(Online::EngineChannelName(release.channel)) + ", not downloaded)");
    }
    m_newProjectEngine->setItems(labels);
    m_newProjectEngine->setSelected(labels.empty() ? -1 : 0);
}

void HubWindow::RefreshEngineList() {
    RefreshNewProjectEngines();
    m_engineRows->clearChildren();
    m_engineRows->add(Heading("Installed"));
    if (m_engines.empty()) {
        auto none = prism::make<prism::Label>(
            "No engine is downloaded yet. Projects can be created now and opened once their engine is here.");
        none->setColor(&prism::Theme::textDim);
        m_engineRows->add(none);
    }
    for (const Online::InstalledEngine& engine : m_engines) {
        const std::string channel(Online::EngineChannelName(engine.channel));
        std::string detail = engine.localBuild ? "Local build: " + engine.install.root.string() : engine.install.root.string();
        prism::Ref<prism::Widget> action;
        if (!engine.localBuild) {
            auto remove = prism::make<prism::Button>("Remove", prism::Icon::Trash, prism::ButtonStyle::Flat);
            const Core::Version version = engine.install.version;
            remove->onClick = [this, version] {
                if (!Online::UninstallEngine(m_context.enginesRoot, version)) {
                    SetStatus("Unable to remove engine " + version.ToString() + ".", true);
                }
                RefreshInstalledEngines();
                RefreshEngineList();
                RefreshProjectList();
            };
            action = remove;
        }
        m_engineRows->add(
            BuildEngineRow("Engine " + engine.install.version.ToString() + "  (" + channel + ")", detail, action));
    }

    if (m_feedResult.state != Online::EngineFeedState::Online) {
        return;
    }
    const Online::EngineReleaseGroups groups = Online::GroupEngineReleases(m_feedResult.releases);
    const auto addRelease = [this](const Online::EngineRelease& release, const std::string& label) {
        const std::string size = LauncherUI::FormatByteSize(release.packageBytes);
        prism::Ref<prism::Widget> action;
        if (IsInstalled(release.version)) {
            auto installed = prism::make<prism::Label>("Installed", prism::FontRole::Small);
            installed->setColor(&prism::Theme::success);
            action = installed;
        } else {
            auto download = prism::make<prism::Button>("Download", prism::Icon::Open, prism::ButtonStyle::Primary);
            download->setEnabled(!IsInstallingEngine());
            download->onClick = [this, release] { DownloadEngine(release); };
            action = download;
        }
        m_engineRows->add(BuildEngineRow(label + "  Engine " + release.version.ToString(),
                                            release.title + " - " + size, action));
    };
    if (groups.stable.has_value() || groups.beta.has_value()) {
        m_engineRows->add(prism::make<prism::Spacer>(0.0F, kRowSpacing));
        m_engineRows->add(Heading("Latest"));
    }
    if (groups.stable.has_value()) {
        addRelease(*groups.stable, "Stable");
    }
    if (groups.beta.has_value()) {
        addRelease(*groups.beta, "Beta");
    }
    if (!groups.previous.empty()) {
        m_engineRows->add(prism::make<prism::Spacer>(0.0F, kRowSpacing));
        m_engineRows->add(Heading("Previous versions"));
        for (const Online::EngineRelease& release : groups.previous) {
            addRelease(release, "Stable");
        }
    }
    m_engineRows->relayout();
}

void HubWindow::CheckForEngines() {
    if (!m_context.feed.has_value()) {
        m_feedStatus->setText("This Hub has no engine release source configured; installed engines are shown.");
        RefreshEngineList();
        return;
    }
    if (m_feedRequest.valid()) {
        return;
    }
    m_feedStatus->setText("Checking for engine versions...");
    const Online::EngineFeedSource source = *m_context.feed;
    const std::string target(Platform::GetEnginePackageTarget());
    m_feedRequest = std::async(std::launch::async, [source, target] {
        const std::unique_ptr<Online::IHttpClient> client = Online::CreatePlatformHttpClient();
        return Online::FetchEngineReleases(*client, source, target);
    });
    RefreshEngineList();
}

void HubWindow::DownloadEngine(const Online::EngineRelease& release) {
    if (IsInstallingEngine()) {
        return;
    }
    m_download = std::make_unique<Online::EngineDownload>(Online::CreatePlatformHttpClient(), release,
                                                             m_context.enginesRoot);
    m_downloadLabel->setText("Downloading engine " + release.version.ToString());
    m_downloadBar->setVisible(true);
    m_downloadBar->parent()->setVisible(true);
    RefreshEngineList();
}

void HubWindow::ImportEnginePackage() {
    if (IsInstallingEngine()) {
        return;
    }
    ShowFilePicker(
        "Import an engine package", m_lastImportFolder, {std::string(Online::kEnginePackageExtension)}, "Import",
        [this](const std::filesystem::path& package) {
            if (IsInstallingEngine()) {
                return;
            }
            const bool isAndroidTempCopy =
#if defined(__ANDROID__)
                package.parent_path().filename() == "picked";
#else
                false;
#endif
            if (!isAndroidTempCopy) {
                m_lastImportFolder = package.parent_path();
            }
            m_import =
                std::async(std::launch::async, [package, enginesRoot = m_context.enginesRoot, isAndroidTempCopy] {
                    const Online::EngineInstallOutcome outcome =
                        Online::InstallLocalEnginePackage(package, enginesRoot);
                    if (isAndroidTempCopy) {
                        std::error_code cleanupError;
                        std::filesystem::remove(package, cleanupError);
                    }
                    return outcome;
                });
            m_importButton->setEnabled(false);
            // An import has no measurable progress; the line shows only what is installing.
            m_downloadLabel->setText("Installing " + package.filename().string() + "...");
            m_downloadBar->setVisible(false);
            m_downloadBar->parent()->setVisible(true);
            RefreshEngineList();
        },
        [this](const std::string& message) { SetStatus(message, true); });
}

void HubWindow::StepStartupLoading() {
    if (!m_loadingOverlay || !m_loadingOverlay->visible()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (m_startupBegan == std::chrono::steady_clock::time_point{}) {
        m_startupBegan = now;
    }
    switch (m_startupStep) {
    case 0:
        m_loadingOverlay->SetProgress("Loading installed engines...", 0.30F, 1.0F);
        RefreshInstalledEngines();
        m_startupStep = 1;
        return;
    case 1:
        m_loadingOverlay->SetProgress("Loading projects...", 0.65F, 1.0F);
        ReloadRegistry();
        m_startupStep = 2;
        return;
    case 2:
        m_loadingOverlay->SetProgress("Preparing templates...", 0.94F, 1.0F);
        RefreshTemplateList();
        // New engine versions only appear when the feed can be reached; offline, nothing is fetched.
        CheckForEngines();
        m_startupStep = 3;
        return;
    case 3: {
        const double shownSeconds = std::chrono::duration<double>(now - m_startupBegan).count();
        if (shownSeconds < kHubSplashMinSeconds) {
            m_loadingOverlay->SetProgress("Ready", 1.0F, 1.0F);
            return;
        }
        if (m_startupFadeBegan == std::chrono::steady_clock::time_point{}) {
            m_startupFadeBegan = now;
        }
        const double fadeElapsed = std::chrono::duration<double>(now - m_startupFadeBegan).count();
        const float t = static_cast<float>(std::clamp(fadeElapsed / kHubSplashFadeSeconds, 0.0, 1.0));
        const float alpha = 1.0F - SmoothStep(t);
        m_loadingOverlay->SetProgress("Ready", 1.0F, alpha);
        if (t >= 1.0F) {
            m_startupStep = 4;
        }
        return;
    }
    default:
        m_loadingOverlay->SetProgress("Ready", 1.0F, 0.0F);
        return;
    }
}

void HubWindow::Tick() {
    StepStartupLoading();
    if (m_feedRequest.valid() &&
        m_feedRequest.wait_for(std::chrono::seconds::zero()) == std::future_status::ready) {
        m_feedResult = m_feedRequest.get();
        switch (m_feedResult.state) {
        case Online::EngineFeedState::Online:
            m_feedStatus->setText(m_feedResult.message);
            break;
        case Online::EngineFeedState::Offline:
            m_feedStatus->setText("You are offline. Installed engines still work; new versions appear when you "
                                  "are back online.");
            break;
        case Online::EngineFeedState::NotFound:
        case Online::EngineFeedState::Failed:
            m_feedStatus->setText(m_feedResult.message);
            break;
        }
        RefreshEngineList();
    }
    if (m_download != nullptr) {
        m_downloadBar->setValue(m_download->Progress());
        const Online::EngineDownload::State state = m_download->GetState();
        if (state != Online::EngineDownload::State::Running) {
            const bool installed = state == Online::EngineDownload::State::Installed;
            SetStatus(m_download->Message(), !installed);
            m_download.reset();
            m_downloadBar->parent()->setVisible(false);
            RefreshInstalledEngines();
            RefreshEngineList();
            RefreshProjectList();
        }
    }
    if (m_import.valid() && m_import.wait_for(std::chrono::seconds::zero()) == std::future_status::ready) {
        const Online::EngineInstallOutcome outcome = m_import.get();
        SetStatus(outcome.message, !outcome.installed);
        m_importButton->setEnabled(true);
        m_downloadBar->parent()->setVisible(false);
        RefreshInstalledEngines();
        RefreshEngineList();
        RefreshProjectList();
    }
}

} // namespace UVE::Hub
