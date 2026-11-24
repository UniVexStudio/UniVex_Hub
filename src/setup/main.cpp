// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "application_runtime.h"
#include "tar_gz_archive.h"
#include "hub_setup.h"
#include "launcher_files.h"
#include "launcher_window.h"
#include "process_launcher.h"
#include "setup_window.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

inline constexpr std::string_view kSetupTitle = "UniVex Hub Setup";
inline constexpr std::string_view kUninstallTitle = "Remove UniVex Hub";
inline constexpr std::uint32_t kSetupWidth = 820U;
inline constexpr std::uint32_t kSetupHeight = 520U;
inline constexpr std::string_view kSetupLogFileName = "univex-hub-setup.log";
inline constexpr int kInvalidCommandLineExitCode = 2;
inline constexpr int kSetupFailedExitCode = 1;
/// How often a silent run checks on the installation.
inline constexpr std::chrono::milliseconds kSilentPollInterval{50};
/// Temporary folder holding the interface assets unpacked from the payload.
inline constexpr std::string_view kInterfaceScratchPrefix = "univex-hub-setup-ui-";
/// Inside that scratch folder: the payload's copy and the folder its interface assets unpack into.
inline constexpr std::string_view kPayloadArchiveStem = "payload";
inline constexpr std::string_view kInterfaceUnpackDirectoryName = "ui";

inline constexpr std::string_view kInstallDirOption = "--install-dir";
inline constexpr std::string_view kSilentOption = "--silent";
inline constexpr std::string_view kUninstallOption = "--uninstall";
inline constexpr std::string_view kNoApplicationsEntryOption = "--no-applications-entry";
inline constexpr std::string_view kEntriesRootOption = "--entries-root";
inline constexpr std::string_view kStepOption = "--step";
inline constexpr std::string_view kFramesOption = "--frames";
inline constexpr std::string_view kScreenshotOption = "--screenshot";
#if defined(_WIN32)
/// Marks the copy of the uninstaller that runs from the temporary folder.
inline constexpr std::string_view kRelocatedOption = "--relocated";
#endif

struct StepChoice final {
    std::string_view word;
    UVE::Setup::SetupStep step;
};
inline constexpr std::array<StepChoice, 3U> kStepChoices{{
    {"welcome", UVE::Setup::SetupStep::Welcome},
    {"location", UVE::Setup::SetupStep::Location},
    {"done", UVE::Setup::SetupStep::Done},
}};

struct SetupOptions final {
    std::filesystem::path installDirectory;
    std::filesystem::path entriesRoot;
    bool silent = false;
    bool uninstall = false;
    bool addToApplications = true;
    bool relocated = false;
    UVE::Setup::SetupStep step = UVE::Setup::SetupStep::Welcome;
    std::optional<int> frameLimit;
    std::filesystem::path screenshot;
    std::optional<std::string> error;
};

[[nodiscard]] SetupOptions ParseOptions(const int argc, char** argv) {
    SetupOptions options;
    const auto value = [&](int& index, const std::string_view option) -> std::optional<std::string> {
        if (index + 1 >= argc || std::string_view{argv[index + 1]}.starts_with("--")) {
            options.error = std::string(option) + " requires a value";
            return std::nullopt;
        }
        return std::string(argv[++index]);
    };
    for (int index = 1; index < argc && !options.error.has_value(); ++index) {
        const std::string_view argument{argv[index]};
        if (argument == kInstallDirOption) {
            if (const std::optional<std::string> text = value(index, argument)) {
                options.installDirectory = *text;
            }
        } else if (argument == kEntriesRootOption) {
            if (const std::optional<std::string> text = value(index, argument)) {
                options.entriesRoot = *text;
            }
        } else if (argument == kSilentOption) {
            options.silent = true;
        } else if (argument == kUninstallOption) {
            options.uninstall = true;
        } else if (argument == kNoApplicationsEntryOption) {
            options.addToApplications = false;
#if defined(_WIN32)
        } else if (argument == kRelocatedOption) {
            options.relocated = true;
#endif
        } else if (argument == kStepOption) {
            if (const std::optional<std::string> text = value(index, argument)) {
                const auto choice = std::find_if(kStepChoices.begin(), kStepChoices.end(),
                                                 [&text](const StepChoice& item) { return item.word == *text; });
                if (choice == kStepChoices.end()) {
                    options.error = std::string(kStepOption) + " must be welcome, location or done";
                } else {
                    options.step = choice->step;
                }
            }
        } else if (argument == kFramesOption) {
            if (const std::optional<std::string> text = value(index, argument)) {
                int frames = 0;
                const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), frames);
                if (error != std::errc{} || end != text->data() + text->size() || frames < 1) {
                    options.error = std::string(kFramesOption) + " requires a positive whole number";
                } else {
                    options.frameLimit = frames;
                }
            }
        } else if (argument == kScreenshotOption) {
            if (const std::optional<std::string> text = value(index, argument)) {
                options.screenshot = *text;
            }
        } else {
            options.error = "unknown option " + std::string(argument);
        }
    }
    return options;
}

#if defined(_WIN32)
/// A running program cannot delete its own file on Windows, so the uninstaller re-runs itself from
/// the temporary folder and lets that copy remove the installation. True when the copy took over.
[[nodiscard]] bool RelaunchOutsideInstallation(const std::filesystem::path& self, int argc, char** argv) {
    std::error_code error;
    const std::filesystem::path copy = std::filesystem::temp_directory_path(error) / self.filename();
    if (error || !std::filesystem::copy_file(self, copy, std::filesystem::copy_options::overwrite_existing, error)) {
        return false;
    }
    std::vector<std::string> arguments(argv + 1, argv + argc);
    arguments.emplace_back(kRelocatedOption);
    if (!UVE::Platform::LaunchDetachedProcess({copy, arguments, {}}).launched) {
        return false;
    }
    // The temporary copy cleans itself up at the next restart.
    MoveFileExW(copy.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return true;
}
#endif

/// A single-file setup has no PRISM assets beside it; they travel in the payload. This unpacks just
/// those into `scratch` and returns their folder, or empty when the payload has none.
[[nodiscard]] std::filesystem::path UnpackInterfaceAssets(const UVE::Setup::SetupContext& context,
                                                             const std::filesystem::path& scratch) {
    if (scratch.empty()) {
        return {};
    }
    const std::filesystem::path unpacked = scratch / kInterfaceUnpackDirectoryName;
    const std::filesystem::path archive =
        scratch / (std::string(kPayloadArchiveStem) + std::string(UVE::Online::kTarGzFileExtension));
    std::error_code error;
    std::filesystem::create_directories(unpacked, error);
    if (error || !context.payload.has_value() ||
        !UVE::Online::ExtractSetupPayload(context.setupExecutable, *context.payload, archive)) {
        return {};
    }
    UVE::Online::ArchiveLimits limits;
    limits.include = [](const std::filesystem::path& path) {
        return !path.empty() && *path.begin() == UVE::LauncherUI::kPrismAssetsDirectoryName;
    };
    const bool extracted = UVE::Online::ExtractTarGz(archive, unpacked, limits).extracted;
    std::filesystem::remove(archive, error);
    return extracted ? unpacked / UVE::LauncherUI::kPrismAssetsDirectoryName : std::filesystem::path{};
}

} // namespace

/// UniVex Hub's installer. With a payload appended (see Engine/Tools/make_hub_installer.py) it
/// installs or updates the Hub and its engine for the current user; `--uninstall` removes it again.
/// `--silent` works without a window; `--install-dir <dir>` chooses the folder;
/// `--no-applications-entry` skips the applications menu; `--entries-root <dir>` writes that entry
/// elsewhere (isolated runs). `--step`, `--frames` and `--screenshot` drive the window for automation.
int main(const int argc, char** argv) {
    const SetupOptions options = ParseOptions(argc, argv);
    if (options.error.has_value() || options.silent) {
        UVE::Platform::AttachParentConsoleForCli();
    }
    if (options.error.has_value()) {
        std::cerr << "uve_hub_setup: " << *options.error << '\n';
        return kInvalidCommandLineExitCode;
    }
    const std::filesystem::path self = UVE::Platform::CurrentExecutablePath();
    std::error_code error;
    const std::filesystem::path selfDirectory =
        self.empty() ? std::filesystem::absolute(std::filesystem::path(argv[0]), error).parent_path() : self.parent_path();

    UVE::Setup::SetupContext context;
    context.mode = options.uninstall ? UVE::Setup::SetupMode::Uninstall : UVE::Setup::SetupMode::Install;
    context.setupExecutable = self;
    context.payload = self.empty() ? std::nullopt : UVE::Online::LocateSetupPayload(self);
    context.entriesRoot = options.entriesRoot;
    context.version = UVE::Online::FormatDisplayVersion(UVE::Online::kHubVersion);
    if (!options.installDirectory.empty()) {
        context.installDirectory = std::filesystem::absolute(options.installDirectory, error);
    } else if (options.uninstall) {
        // The uninstaller lives inside the installation it removes.
        context.installDirectory = selfDirectory;
    } else if (const std::optional<std::filesystem::path> fallback = UVE::Online::DefaultHubInstallDirectory()) {
        context.installDirectory = *fallback;
    }

#if defined(_WIN32)
    if (options.uninstall && !options.relocated &&
        selfDirectory.lexically_normal() == context.installDirectory.lexically_normal() &&
        RelaunchOutsideInstallation(self, argc, argv)) {
        return 0;
    }
#endif

    if (options.silent) {
        UVE::Setup::SetupJob job(context, options.addToApplications);
        while (!job.IsFinished()) {
            std::this_thread::sleep_for(kSilentPollInterval);
        }
        const UVE::Online::HubSetupResult result = job.Result();
        (result.succeeded ? std::cout : std::cerr) << "uve_hub_setup: " << result.message << '\n';
        return result.succeeded ? 0 : kSetupFailedExitCode;
    }

    UVE::LauncherUI::LauncherWindowDesc window;
    window.title = std::string(options.uninstall ? kUninstallTitle : kSetupTitle);
    window.width = kSetupWidth;
    window.height = kSetupHeight;
    // Assets beside the program (a development build, or the uninstaller inside an installation)
    // are used as they are; a downloaded setup unpacks its own.
    window.prismAssetsDirectory = selfDirectory / UVE::LauncherUI::kPrismAssetsDirectoryName;
    const std::filesystem::path scratch = UVE::LauncherUI::UniqueTemporaryPath(kInterfaceScratchPrefix, {});
    if (!std::filesystem::is_directory(window.prismAssetsDirectory, error)) {
        window.prismAssetsDirectory = UnpackInterfaceAssets(context, scratch);
    }
    window.logFile = std::filesystem::temp_directory_path(error) / kSetupLogFileName;
    window.commandLine = std::vector<std::string>(argv + 1, argv + argc);
    window.frameLimit = options.frameLimit;
    window.screenshot = options.screenshot;

    UVE::Setup::SetupWindow setup(std::move(context));
    const int exitCode = UVE::LauncherUI::RunLauncherWindow(
        window,
        [&setup, &options] {
            prism::Ref<prism::Widget> root = setup.Build();
            setup.ShowStep(options.step);
            return root;
        },
        [&setup] { setup.Tick(); });
    std::filesystem::remove_all(scratch, error);
    return exitCode;
}
