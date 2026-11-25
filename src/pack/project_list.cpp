// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_list.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

#include <nlohmann/json.hpp>

#include "application_runtime.h"
#include "pack_json.h"

namespace UVE::Pack {
namespace {

using Json = nlohmann::json;

/// Suffix of the temporary sibling the settings are written through.
inline constexpr std::string_view kTemporaryFileSuffix = ".tmp";
inline constexpr std::string_view kMissingBadge = "Missing";
inline constexpr std::string_view kEngineBadgePrefix = "Engine ";

[[nodiscard]] std::string Lower(const std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

[[nodiscard]] ProjectListStatus StatusFor(const ProjectCompatibilityVerdict verdict) noexcept {
    switch (verdict) {
    case ProjectCompatibilityVerdict::Same:
    case ProjectCompatibilityVerdict::OlderUpgradesSilently:
        return ProjectListStatus::Ready;
    case ProjectCompatibilityVerdict::OlderNeedsUpgrade:
        return ProjectListStatus::NeedsUpgrade;
    case ProjectCompatibilityVerdict::NewerCompatible:
        return ProjectListStatus::OpensWithWarning;
    case ProjectCompatibilityVerdict::NewerIncompatible:
        return ProjectListStatus::Incompatible;
    }
    return ProjectListStatus::Incompatible;
}

} // namespace

std::vector<ProjectListRow> BuildProjectList(const std::vector<ProjectRegistryEntry>& entries,
                                                   const std::string_view query, const ProjectListSort sort,
                                                   const Core::Version& engineVersion,
                                                   const std::function<bool(const ProjectRegistryEntry&)>& isMissing) {
    const std::string needle = Lower(query);
    std::vector<ProjectListRow> rows;
    rows.reserve(entries.size());
    for (const ProjectRegistryEntry& entry : entries) {
        if (!needle.empty() && Lower(entry.displayName).find(needle) == std::string::npos &&
            Lower(entry.projectFile.string()).find(needle) == std::string::npos &&
            Lower(entry.projectFile.generic_string()).find(needle) == std::string::npos) {
            continue;
        }
        ProjectListRow row;
        row.entry = entry;
        if (isMissing && isMissing(entry)) {
            row.status = ProjectListStatus::Missing;
            row.badge = kMissingBadge;
        } else {
            row.status = StatusFor(EvaluateProjectCompatibility(entry.engineVersion, engineVersion).verdict);
            row.badge = std::string(kEngineBadgePrefix) + FormatProjectVersion(entry.engineVersion);
        }
        rows.push_back(std::move(row));
    }
    // Registry order is most recent first; a stable sort keeps it among equals.
    std::stable_sort(rows.begin(), rows.end(), [sort](const ProjectListRow& lhs, const ProjectListRow& rhs) {
        if (lhs.entry.favorite != rhs.entry.favorite) {
            return lhs.entry.favorite;
        }
        if (sort == ProjectListSort::Name) {
            return Lower(lhs.entry.displayName) < Lower(rhs.entry.displayName);
        }
        return lhs.entry.lastOpenedUnixSeconds > rhs.entry.lastOpenedUnixSeconds;
    });
    return rows;
}

ProjectListStatus ProjectListStatusFor(const Platform::EditorProjectVersion& projectVersion,
                                             const Core::Version& engineVersion) {
    return StatusFor(EvaluateProjectCompatibility(projectVersion, engineVersion).verdict);
}

LauncherSettings LoadLauncherSettings(const std::filesystem::path& file) {
    LauncherSettings settings;
    const Platform::ApplicationUserDataDirectoryResult documents = Platform::ResolveUserDocumentsDirectory();
    if (documents.success) {
        settings.defaultProjectLocation = documents.directory / kDefaultProjectsDirectoryName;
    } else if (const Platform::ApplicationUserDataDirectoryResult home = Platform::ResolveUserHomeDirectory();
               home.success) {
        settings.defaultProjectLocation = home.directory / kDefaultProjectsDirectoryName;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        return settings;
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kLauncherSettingsFormat ||
        json.value("schemaVersion", 0U) != kCurrentLauncherSettingsSchemaVersion) {
        return settings;
    }
    const std::string location = json.value("defaultProjectLocation", std::string{});
    if (!location.empty() && std::filesystem::path(location).is_absolute()) {
#if defined(__ANDROID__)
        // Auto-migrate legacy private internal /data/user/... default to user-visible external storage.
        const Platform::ApplicationUserDataDirectoryResult internalHome = Platform::ResolveUserHomeDirectory();
        const bool isLegacyPrivateDefault =
            internalHome.success &&
            std::filesystem::path(location).lexically_normal() ==
                (internalHome.directory / kDefaultProjectsDirectoryName).lexically_normal();
        if (!isLegacyPrivateDefault) {
            settings.defaultProjectLocation = location;
        }
#else
        settings.defaultProjectLocation = location;
#endif
    }
    return settings;
}

bool SaveLauncherSettings(const std::filesystem::path& file, const LauncherSettings& settings) {
    const Json json{{"format", kLauncherSettingsFormat},
                       {"schemaVersion", kCurrentLauncherSettingsSchemaVersion},
                       {"defaultProjectLocation", settings.defaultProjectLocation.generic_string()}};
    std::error_code error;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
        if (error) {
            return false;
        }
    }
    std::filesystem::path temporary = file;
    temporary += kTemporaryFileSuffix;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << json.dump(kPackJsonIndent);
        if (!output.good()) {
            return false;
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

bool IsProjectManifestFileName(const std::filesystem::path& path) {
    const std::string filename = Lower(path.filename().string());
    if (filename.empty()) {
        return false;
    }
    const std::string ext = Lower(path.extension().string());
    return ext == Platform::kEditorProjectFileExtension || filename == Platform::kEditorProjectFileExtension;
}

ProjectFolderLookupResult FindProjectManifestInFolder(const std::filesystem::path& path,
                                                      const bool storageAccessRestricted) {
    constexpr std::string_view kLegacyProjectExtension = ".uveditor";
    if (path.empty()) {
        return {std::nullopt, {}, false, "Choose a project folder or .uvproject file."};
    }

    std::error_code statusError;
    if (std::filesystem::is_regular_file(path, statusError)) {
        if (IsProjectManifestFileName(path)) {
            return {path, {path}, false, {}};
        }
        const std::string ext = Lower(path.extension().string());
        if (ext == kLegacyProjectExtension) {
            std::filesystem::path migrated = path;
            migrated.replace_extension(Platform::kEditorProjectFileExtension);
            return {migrated, {migrated}, false, {}};
        }
        return {std::nullopt, {}, false,
                "That file is not a UniVex project (" + std::string(Platform::kEditorProjectFileExtension) + ")."};
    }

    statusError.clear();
    if (!std::filesystem::is_directory(path, statusError)) {
        if (storageAccessRestricted) {
            return {std::nullopt, {}, true,
                    "Storage access is required to open that folder. Tap 'Allow Storage' to grant All Files Access."};
        }
        return {std::nullopt, {}, statusError.value() != 0,
                statusError ? "Unable to access that folder: " + statusError.message()
                            : "That folder does not exist."};
    }

    std::vector<std::filesystem::path> manifests;
    std::vector<std::filesystem::path> legacyManifests;
    std::error_code iterError;
    std::filesystem::directory_iterator iterator(path, std::filesystem::directory_options::skip_permission_denied,
                                                 iterError);
    if (iterError) {
        return {std::nullopt, {}, true,
                storageAccessRestricted
                    ? "Storage access is required to read .uvproject files here. Tap 'Allow Storage' to grant All "
                      "Files Access."
                    : "Unable to read that folder: " + iterError.message()};
    }

    for (const std::filesystem::directory_entry& entry : iterator) {
        std::error_code entryError;
        if (!entry.is_regular_file(entryError) || entryError) {
            continue;
        }
        const std::filesystem::path entryPath = entry.path();
        if (IsProjectManifestFileName(entryPath)) {
            manifests.push_back(entryPath);
        } else if (Lower(entryPath.extension().string()) == kLegacyProjectExtension) {
            legacyManifests.push_back(entryPath);
        }
    }

    std::sort(manifests.begin(), manifests.end());
    if (manifests.size() == 1U) {
        return {manifests.front(), manifests, false, {}};
    }
    if (manifests.size() > 1U) {
        return {std::nullopt, manifests, false,
                "That folder has more than one project file; select the .uvproject file directly."};
    }

    std::sort(legacyManifests.begin(), legacyManifests.end());
    if (legacyManifests.size() == 1U) {
        std::filesystem::path migrated = legacyManifests.front();
        migrated.replace_extension(Platform::kEditorProjectFileExtension);
        return {migrated, {migrated}, false, {}};
    }
    if (legacyManifests.size() > 1U) {
        return {std::nullopt, legacyManifests, false,
                "That folder has more than one project file; select one directly."};
    }

    if (storageAccessRestricted) {
        return {std::nullopt, {}, true,
                "Android hides .uvproject files in shared storage when All Files Access is off. Tap 'Allow Storage' "
                "in the folder picker, then try again."};
    }
    return {std::nullopt, {}, false,
            "That folder has no project file (" + std::string(Platform::kEditorProjectFileExtension) + ")."};
}

} // namespace UVE::Pack
