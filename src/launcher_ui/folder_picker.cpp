// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "folder_picker.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

#include "application_runtime.h"
#include "launcher_widgets.h"
#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cwchar>
#include <windows.h>
#endif

namespace UVE::LauncherUI {
namespace {

inline constexpr float kPickerWidth = 540.0F;
inline constexpr float kPickerHorizontalMargin = 20.0F;
inline constexpr float kPickerMinWidth = 260.0F;
inline constexpr float kPickerListHeight = 260.0F;
inline constexpr float kPickerSpacing = 6.0F;
/// Leading character of a hidden folder on the platforms that hide by name.
inline constexpr char kHiddenPrefix = '.';

std::function<bool()> g_hasStorageAccess;
std::function<void()> g_requestStorageAccess;

[[nodiscard]] float ResponsivePickerWidth() {
    if (!prism::Application::exists()) {
        return kPickerWidth;
    }
    const float available = prism::Application::get().windowSize().x - kPickerHorizontalMargin;
    return std::clamp(available, kPickerMinWidth, kPickerWidth);
}

[[nodiscard]] float ResponsivePickerListHeight() {
    if (!prism::Application::exists()) {
        return kPickerListHeight;
    }
    const float h = prism::Application::get().windowSize().y;
    return std::clamp(h * 0.36F, 96.0F, kPickerListHeight);
}

/// The picker's state, shared by its widgets' callbacks for as long as the window is open.
struct FolderPickerState final {
    std::filesystem::path current;
    prism::Ref<prism::TextInput> path;
    prism::Ref<prism::WrapBox> breadcrumbs;
    prism::Ref<prism::Box> folders;
    prism::Ref<prism::Label> problem;
    prism::Ref<prism::TextInput> newFolder;
    prism::Ref<prism::Button> grantButton;
    int permissionTimerId = 0;
    /// File mode: the name endings that are listed, and the file selected so far. Empty in folder mode.
    std::vector<std::string> fileSuffixes;
    std::optional<std::filesystem::path> selectedFile;
    std::vector<prism::Ref<NavItem>> fileRows;
};

[[nodiscard]] std::string Lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

[[nodiscard]] bool IsHiddenName(const std::string& name) {
    return name.empty() || name.front() == kHiddenPrefix;
}

void SortByName(std::vector<std::filesystem::path>& paths) {
    std::sort(paths.begin(), paths.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
        return lhs.filename().string() < rhs.filename().string();
    });
}

[[nodiscard]] bool FolderContainsProjectFile(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied,
                                                 error);
    if (error) {
        return false;
    }
    int checked = 0;
    for (const std::filesystem::directory_entry& entry : iterator) {
        if (++checked > 64) {
            break;
        }
        if (!entry.is_regular_file(error)) {
            continue;
        }
        if (Lowercase(entry.path().extension().string()) == ".uvproject") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool CanReadDirectory(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    return !error;
}

/// Where a picker opens: the nearest existing folder of `start`, else Documents / public storage / home.
[[nodiscard]] std::filesystem::path FirstFolder(const std::filesystem::path& start) {
    std::error_code error;
    std::filesystem::path first = start;
    // Start at the nearest folder that exists, so a location not created yet still opens nearby.
    while (!first.empty() && !std::filesystem::is_directory(first, error) && first.has_parent_path() &&
           first.parent_path() != first) {
        first = first.parent_path();
    }
    if (first.empty() || !std::filesystem::is_directory(first, error)) {
        const Platform::ApplicationUserDataDirectoryResult docs = Platform::ResolveUserDocumentsDirectory();
        if (docs.success && std::filesystem::is_directory(docs.directory, error)) {
            first = docs.directory;
        } else if (const Platform::ApplicationUserDataDirectoryResult storage =
                       Platform::ResolvePublicStorageRoot();
                   storage.success && std::filesystem::is_directory(storage.directory, error)) {
            first = storage.directory;
        } else {
            const Platform::ApplicationUserDataDirectoryResult home = Platform::ResolveUserHomeDirectory();
            first = home.success ? home.directory : std::filesystem::current_path(error);
        }
    }
    return first;
}

void ShowFolder(const std::shared_ptr<FolderPickerState>& state, const std::filesystem::path& directory);

void ReleasePickerState(const std::shared_ptr<FolderPickerState>& state) {
    if (!state) {
        return;
    }
    if (state->permissionTimerId != 0 && prism::Application::exists()) {
        prism::Application::get().cancelTimer(state->permissionTimerId);
    }
    state->permissionTimerId = 0;
    state->fileRows.clear();
    if (state->breadcrumbs) {
        state->breadcrumbs->clearChildren();
    }
    if (state->folders) {
        state->folders->clearChildren();
    }
}

void RebuildBreadcrumbs(const std::shared_ptr<FolderPickerState>& state) {
    if (!state || !state->breadcrumbs) {
        return;
    }
    state->breadcrumbs->clearChildren();
    if (state->current.empty()) {
        auto rootLabel = prism::make<prism::Label>("Drives", prism::FontRole::Small);
        rootLabel->setColor(&prism::Theme::textDim);
        state->breadcrumbs->add(rootLabel);
        state->breadcrumbs->relayout();
        return;
    }

    std::vector<std::pair<std::string, std::filesystem::path>> crumbs;
    std::filesystem::path cursor = state->current;
    while (!cursor.empty()) {
        std::string label = cursor.filename().string();
        if (label.empty()) {
            label = cursor.string();
        }
        crumbs.emplace_back(std::move(label), cursor);
        if (!cursor.has_parent_path() || cursor.parent_path() == cursor) {
            break;
        }
        cursor = cursor.parent_path();
    }
    std::reverse(crumbs.begin(), crumbs.end());

    // Keep the breadcrumb bar compact on deep paths by collapsing leading ancestors into "...".
    constexpr std::size_t kMaxCrumbs = 5U;
    std::size_t startIdx = 0U;
    if (crumbs.size() > kMaxCrumbs) {
        startIdx = crumbs.size() - kMaxCrumbs;
        auto dots = prism::make<prism::Button>("...", prism::Icon::None, prism::ButtonStyle::Flat);
        const std::filesystem::path parentTarget = crumbs[startIdx - 1U].second;
        dots->setTooltip(parentTarget.string());
        dots->onClick = [weak = std::weak_ptr<FolderPickerState>(state), parentTarget] {
            if (const std::shared_ptr<FolderPickerState> owner = weak.lock()) {
                ShowFolder(owner, parentTarget);
            }
        };
        state->breadcrumbs->add(dots);
    }

    for (std::size_t i = startIdx; i < crumbs.size(); ++i) {
        if (i > startIdx || startIdx > 0U) {
            auto sep = prism::make<prism::Label>(">", prism::FontRole::Small);
            sep->setColor(&prism::Theme::textDim);
            state->breadcrumbs->add(sep, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        }
        const bool currentLeaf = (i + 1U == crumbs.size());
        auto crumbBtn = prism::make<prism::Button>(
            crumbs[i].first, prism::Icon::None,
            currentLeaf ? prism::ButtonStyle::Toolbar : prism::ButtonStyle::Flat);
        crumbBtn->setTooltip(crumbs[i].second.string());
        const std::filesystem::path target = crumbs[i].second;
        crumbBtn->onClick = [weak = std::weak_ptr<FolderPickerState>(state), target] {
            if (const std::shared_ptr<FolderPickerState> owner = weak.lock()) {
                ShowFolder(owner, target);
            }
        };
        state->breadcrumbs->add(crumbBtn);
    }
    state->breadcrumbs->relayout();
}

#if defined(_WIN32)
/// Lists Windows logical drives (C:\, D:\, ...) when moving up from a drive root.
void ShowWindowsDrives(const std::shared_ptr<FolderPickerState>& state) {
    state->current.clear();
    state->path->setText("");
    state->problem->setText("");
    state->folders->clearChildren();
    state->selectedFile.reset();
    state->fileRows.clear();
    RebuildBreadcrumbs(state);

    const DWORD needed = GetLogicalDriveStringsW(0, nullptr);
    std::vector<std::filesystem::path> drives;
    if (needed > 0U) {
        std::vector<wchar_t> buffer(static_cast<std::size_t>(needed) + 1U, L'\0');
        if (GetLogicalDriveStringsW(needed, buffer.data()) > 0U) {
            for (const wchar_t* cursor = buffer.data(); *cursor != L'\0'; cursor += std::wcslen(cursor) + 1U) {
                drives.emplace_back(cursor);
            }
        }
    }
    for (const std::filesystem::path& drive : drives) {
        auto row = prism::make<NavItem>(drive.string(), prism::Icon::Folder);
        const std::weak_ptr<FolderPickerState> weak = state;
        row->onActivate = [weak, drive] {
            if (const std::shared_ptr<FolderPickerState> owner = weak.lock()) {
                ShowFolder(owner, drive);
            }
        };
        state->folders->add(row, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }
    state->folders->relayout();
}
#endif

void ShowFolder(const std::shared_ptr<FolderPickerState>& state, const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        state->problem->setText("That folder does not exist or requires storage access.");
        return;
    }
    state->current = directory.lexically_normal();
    state->path->setText(state->current.string());
    state->problem->setText("");
    state->folders->clearChildren();
    state->selectedFile.reset();
    state->fileRows.clear();
    RebuildBreadcrumbs(state);

    const bool pickFiles = !state->fileSuffixes.empty();
    const bool readable = CanReadDirectory(state->current);
    const std::vector<std::filesystem::path> children = ListPickableFolders(state->current);
    const std::vector<std::filesystem::path> files =
        pickFiles ? ListPickableFiles(state->current, state->fileSuffixes) : std::vector<std::filesystem::path>{};
    if (children.empty() && files.empty()) {
        const bool needsStorage = !readable && g_hasStorageAccess && !g_hasStorageAccess();
        const char* message = nullptr;
        if (needsStorage) {
            message = "Storage access is required here. Tap 'Allow Storage' above to browse Documents & Downloads.";
        } else if (!readable) {
            message = "This folder cannot be read. Choose another place above.";
        } else if (pickFiles) {
            message = "No matching files here. Open another folder.";
        } else {
            message = "Empty folder. Tap 'Choose this folder' or create a new folder below.";
        }
        auto empty = prism::make<prism::Label>(message, prism::FontRole::Small);
        empty->setColor(&prism::Theme::textDim);
        auto emptyPad = prism::vbox();
        emptyPad->setPadding(prism::Margin(8.0F, 8.0F, 8.0F, 8.0F));
        emptyPad->add(empty);
        state->folders->add(emptyPad);
    }
    for (const std::filesystem::path& child : children) {
        const bool hasProject = !pickFiles && FolderContainsProjectFile(child);
        const std::string label =
            hasProject ? (child.filename().string() + "  [Project]") : child.filename().string();
        auto row = prism::make<NavItem>(label, hasProject ? prism::Icon::FolderOpen : prism::Icon::Folder);
        const std::weak_ptr<FolderPickerState> weak = state;
        row->onActivate = [weak, child] {
            if (const std::shared_ptr<FolderPickerState> owner = weak.lock()) {
                ShowFolder(owner, child);
            }
        };
        state->folders->add(row, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }
    for (const std::filesystem::path& file : files) {
        auto row = prism::make<NavItem>(file.filename().string(), prism::Icon::File);
        const std::weak_ptr<FolderPickerState> weak = state;
        NavItem* const self = row.get();
        row->onActivate = [weak, file, self] {
            if (const std::shared_ptr<FolderPickerState> owner = weak.lock()) {
                owner->selectedFile = file;
                for (const prism::Ref<NavItem>& other : owner->fileRows) {
                    other->SetActive(other.get() == self);
                }
            }
        };
        state->fileRows.push_back(row);
        state->folders->add(row, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }
    state->folders->relayout();
}

/// The path field, wrapping quick-jump places bar, breadcrumbs, folder list, and problem line.
[[nodiscard]] prism::Ref<prism::Box> BuildPickerBody(const std::shared_ptr<FolderPickerState>& state) {
    auto body = prism::vbox(kPickerSpacing);
    auto navigation = prism::hbox(kPickerSpacing);
    auto up = prism::make<prism::Button>("", prism::Icon::ChevronUp);
    up->setTooltip("Up one folder");
    up->onClick = [weak = std::weak_ptr<FolderPickerState>(state)] {
        const std::shared_ptr<FolderPickerState> owner = weak.lock();
        if (!owner || owner->current.empty()) {
            return;
        }
        if (owner->current.has_parent_path() && owner->current.parent_path() != owner->current) {
            ShowFolder(owner, owner->current.parent_path());
        }
#if defined(_WIN32)
        else {
            ShowWindowsDrives(owner);
        }
#endif
    };
    navigation->add(up);
    state->path = prism::make<prism::TextInput>();
    state->path->onCommit = [weak = std::weak_ptr<FolderPickerState>(state)](const std::string& typed) {
        if (const std::shared_ptr<FolderPickerState> owner = weak.lock()) {
            ShowFolder(owner, typed);
        }
    };
    navigation->add(state->path, prism::Slot{1.0F});
    auto refresh = prism::make<prism::Button>("", prism::Icon::Refresh);
    refresh->setTooltip("Refresh current folder");
    refresh->onClick = [weak = std::weak_ptr<FolderPickerState>(state)] {
        if (const std::shared_ptr<FolderPickerState> owner = weak.lock()) {
            if (!owner->current.empty()) {
                ShowFolder(owner, owner->current);
            }
        }
    };
    navigation->add(refresh);
    body->add(navigation);

    // Quick-jump places bar using WrapBox so every shortcut stays tappable on narrow mobile screens.
    auto shortcuts = prism::make<prism::WrapBox>(prism::Orientation::Horizontal, 4.0F, 4.0F);
    const auto addJump = [&](const std::string& label, const std::filesystem::path& target) {
        if (target.empty()) {
            return;
        }
        auto chip = prism::make<prism::Button>(label, prism::Icon::Folder, prism::ButtonStyle::Toolbar);
        chip->setTooltip(target.string());
        chip->onClick = [weak = std::weak_ptr<FolderPickerState>(state), target] {
            if (const std::shared_ptr<FolderPickerState> owner = weak.lock()) {
                std::error_code mkErr;
                if (!std::filesystem::is_directory(target, mkErr)) {
                    std::filesystem::create_directories(target, mkErr);
                }
                ShowFolder(owner, target);
            }
        };
        shortcuts->add(chip);
    };
#if defined(__ANDROID__)
    if (const Platform::ApplicationUserDataDirectoryResult storage = Platform::ResolvePublicStorageRoot();
        storage.success) {
        addJump("Storage", storage.directory);
    }
    if (const char* const pubDocs = std::getenv("UNIVEX_ANDROID_PUBLIC_DOCS"); pubDocs != nullptr && pubDocs[0] != '\0') {
        addJump("Documents", std::filesystem::path(pubDocs));
    } else if (const Platform::ApplicationUserDataDirectoryResult docs = Platform::ResolveUserDocumentsDirectory();
               docs.success) {
        addJump("Documents", docs.directory);
    }
    if (const Platform::ApplicationUserDataDirectoryResult dl = Platform::ResolveUserDownloadsDirectory();
        dl.success) {
        addJump("Downloads", dl.directory);
    }
    if (const char* const appDocs = std::getenv("UNIVEX_ANDROID_APP_EXT_DOCS"); appDocs != nullptr && appDocs[0] != '\0') {
        addJump("App Files", std::filesystem::path(appDocs));
    }
    // Surface mounted external SD cards / USB volumes under /storage/ when readable.
    {
        std::error_code storageErr;
        std::filesystem::directory_iterator storageIt(
            "/storage", std::filesystem::directory_options::skip_permission_denied, storageErr);
        if (!storageErr) {
            for (const std::filesystem::directory_entry& entry : storageIt) {
                const std::string vol = entry.path().filename().string();
                if (vol.empty() || vol == "self" || vol == "emulated" || vol.front() == '.') {
                    continue;
                }
                if (entry.is_directory(storageErr)) {
                    addJump("SD: " + vol, entry.path());
                }
            }
        }
    }
    if (g_hasStorageAccess && g_requestStorageAccess && !g_hasStorageAccess()) {
        state->grantButton = prism::make<prism::Button>("Allow Storage", prism::Icon::Lock, prism::ButtonStyle::Primary);
        state->grantButton->setTooltip("Grant Android All Files Access to browse Documents & Downloads");
        state->grantButton->onClick = [weak = std::weak_ptr<FolderPickerState>(state)] {
            if (g_requestStorageAccess) {
                g_requestStorageAccess();
            }
            if (const std::shared_ptr<FolderPickerState> owner = weak.lock()) {
                owner->problem->setText("Grant access in Android Settings; this folder will refresh automatically.");
            }
        };
        shortcuts->add(state->grantButton);

        // Automatically refresh as soon as the user returns from Android Settings with permission granted.
        if (prism::Application::exists()) {
            const std::weak_ptr<FolderPickerState> weak = state;
            state->permissionTimerId = prism::Application::get().addTimer(500, [weak]() -> bool {
                const std::shared_ptr<FolderPickerState> owner = weak.lock();
                if (!owner || !g_hasStorageAccess) {
                    return false;
                }
                if (g_hasStorageAccess()) {
                    owner->permissionTimerId = 0;
                    if (owner->grantButton) {
                        owner->grantButton->setVisible(false);
                    }
                    if (!owner->current.empty()) {
                        ShowFolder(owner, owner->current);
                    }
                    return false;
                }
                return true;
            });
        }
    }
#else
    if (const Platform::ApplicationUserDataDirectoryResult docs = Platform::ResolveUserDocumentsDirectory();
        docs.success) {
        addJump("Documents", docs.directory);
    }
    if (const Platform::ApplicationUserDataDirectoryResult dl = Platform::ResolveUserDownloadsDirectory();
        dl.success) {
        addJump("Downloads", dl.directory);
    }
    if (const Platform::ApplicationUserDataDirectoryResult home = Platform::ResolveUserHomeDirectory();
        home.success) {
        std::error_code err;
        const std::filesystem::path desktop = home.directory / "Desktop";
        if (std::filesystem::is_directory(desktop, err)) {
            addJump("Desktop", desktop);
        }
        addJump("Home", home.directory);
    }
#if defined(_WIN32)
    auto drivesChip = prism::make<prism::Button>("Drives", prism::Icon::FolderOpen, prism::ButtonStyle::Toolbar);
    drivesChip->setTooltip("Show all drives");
    drivesChip->onClick = [weak = std::weak_ptr<FolderPickerState>(state)] {
        if (const std::shared_ptr<FolderPickerState> owner = weak.lock()) {
            ShowWindowsDrives(owner);
        }
    };
    shortcuts->add(drivesChip);
#else
    addJump("Computer", std::filesystem::path{"/"});
#endif
#endif
    body->add(shortcuts);

    // Clickable breadcrumb bar for fast parent navigation.
    state->breadcrumbs = prism::make<prism::WrapBox>(prism::Orientation::Horizontal, 2.0F, 2.0F);
    auto crumbBar = prism::make<prism::Border>();
    crumbBar->setBackground(&prism::Theme::header)
        .setBorder(&prism::Theme::borderSoft)
        .setPadding(prism::Margin(4.0F, 2.0F, 4.0F, 2.0F))
        .setRadius(3.0F)
        .setChild(state->breadcrumbs);
    body->add(crumbBar);

    state->folders = prism::vbox();
    auto list = prism::make<prism::ScrollView>();
    list->setContent(state->folders);
    list->setFixedHeight(ResponsivePickerListHeight());
    auto frame = prism::make<prism::Border>();
    frame->setBackground(&prism::Theme::field).setBorder(&prism::Theme::borderSoft).setChild(list);
    body->add(frame);

    state->problem = prism::make<prism::Label>("", prism::FontRole::Small);
    state->problem->setColor(&prism::Theme::error);
    return body;
}

} // namespace

std::vector<std::filesystem::path> ListPickableFolders(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> folders;
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied,
                                                 error);
    if (error) {
        return folders;
    }
    for (const std::filesystem::directory_entry& entry : iterator) {
        if (IsHiddenName(entry.path().filename().string()) || !entry.is_directory(error)) {
            continue;
        }
        folders.push_back(entry.path());
    }
    SortByName(folders);
    return folders;
}

std::vector<std::filesystem::path> ListPickableFiles(const std::filesystem::path& directory,
                                                        const std::vector<std::string>& suffixes) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied,
                                                 error);
    if (error) {
        return files;
    }
    for (const std::filesystem::directory_entry& entry : iterator) {
        const std::string name = Lowercase(entry.path().filename().string());
        if (IsHiddenName(name) || !entry.is_regular_file(error)) {
            continue;
        }
        const bool matches = std::any_of(suffixes.begin(), suffixes.end(), [&name](const std::string& suffix) {
            const std::string wanted = Lowercase(suffix);
            return name.size() > wanted.size() && name.compare(name.size() - wanted.size(), wanted.size(), wanted) == 0;
        });
        if (matches) {
            files.push_back(entry.path());
        }
    }
    SortByName(files);
    return files;
}

void ShowFolderPicker(const std::string& title, const std::filesystem::path& start,
                         std::function<void(const std::filesystem::path&)> onPicked) {
    auto state = std::make_shared<FolderPickerState>();
    const prism::Ref<prism::Box> body = BuildPickerBody(state);

    auto creation = prism::hbox(kPickerSpacing);
    state->newFolder = prism::make<prism::TextInput>("", "New folder name");
    creation->add(state->newFolder, prism::Slot{1.0F});
    auto make = prism::make<prism::Button>("Make folder", prism::Icon::Plus);
    make->onClick = [state] {
        if (state->current.empty()) {
            state->problem->setText("Choose a drive first.");
            return;
        }
        const std::string name = state->newFolder->text();
        const std::filesystem::path created = state->current / name;
        std::error_code makeError;
        if (name.empty() || std::filesystem::path(name).has_parent_path() ||
            !std::filesystem::create_directory(created, makeError)) {
            state->problem->setText(makeError ? "Unable to make that folder: " + makeError.message()
                                              : "Enter a new folder name without slashes.");
            return;
        }
        state->newFolder->setText("");
        ShowFolder(state, created);
    };
    creation->add(make);
    body->add(creation);
    body->add(state->problem);

    ShowFolder(state, FirstFolder(start));
    prism::showDialog(title, body,
                      {{"Cancel", false, [state] { ReleasePickerState(state); }},
                       {"Choose this folder", true,
                        [state, onPicked = std::move(onPicked)] {
                            const std::filesystem::path chosen = state->current;
                            ReleasePickerState(state);
                            if (!chosen.empty() && onPicked) {
                                onPicked(chosen);
                            }
                        }}},
                      ResponsivePickerWidth());
}

namespace {
SystemFilePicker g_systemFilePicker;
} // namespace

void SetSystemFilePicker(SystemFilePicker picker) {
    g_systemFilePicker = std::move(picker);
}

void SetStorageAccessProvider(std::function<bool()> hasAccess, std::function<void()> requestAccess) {
    g_hasStorageAccess = std::move(hasAccess);
    g_requestStorageAccess = std::move(requestAccess);
}

void ShowFilePicker(const std::string& title, const std::filesystem::path& start, std::vector<std::string> suffixes,
                       const std::string& confirmLabel, std::function<void(const std::filesystem::path&)> onPicked,
                       std::function<void(const std::string&)> onError) {
    if (g_systemFilePicker && g_systemFilePicker(suffixes, onPicked, std::move(onError))) {
        return;
    }
    auto state = std::make_shared<FolderPickerState>();
    state->fileSuffixes = std::move(suffixes);
    const prism::Ref<prism::Box> body = BuildPickerBody(state);
    body->add(state->problem);

    ShowFolder(state, FirstFolder(start));
    // The rows hold the state weakly; clearing them on close releases everything.
    const auto release = [state] { ReleasePickerState(state); };
    prism::showDialog(title, body,
                      {{"Cancel", false, release},
                       {confirmLabel, true,
                        [state, release, onPicked = std::move(onPicked)] {
                            const std::optional<std::filesystem::path> chosen = state->selectedFile;
                            release();
                            if (chosen.has_value() && onPicked) {
                                onPicked(*chosen);
                            }
                        }}},
                      ResponsivePickerWidth());
}

} // namespace UVE::LauncherUI
