// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace UVE::LauncherUI {

/// The folders directly inside `directory` that a person would pick from: sorted by name, without
/// hidden ones. Unreadable directories list as empty.
[[nodiscard]] std::vector<std::filesystem::path> ListPickableFolders(const std::filesystem::path& directory);

/// The files directly inside `directory` whose names end with one of `suffixes` (case-insensitive),
/// sorted by name, without hidden ones.
[[nodiscard]] std::vector<std::filesystem::path> ListPickableFiles(const std::filesystem::path& directory,
                                                                      const std::vector<std::string>& suffixes);

/// A platform's own file picker (Android's, which reaches the shared folders an app may not list).
/// It shows itself for files ending with one of `suffixes` and calls `onPicked` with the chosen file
/// later, from the UI thread (or `onError` when a pick fails or is rejected); it returns false when
/// it cannot open, and PRISM's picker is used.
using SystemFilePicker = std::function<bool(const std::vector<std::string>& suffixes,
                                               std::function<void(const std::filesystem::path&)> onPicked,
                                               std::function<void(const std::string&)> onError)>;
/// Makes ShowFilePicker use `picker` (an empty one restores PRISM's).
void SetSystemFilePicker(SystemFilePicker picker);

/// Registers platform callbacks for checking and requesting broad external storage access
/// (Android All Files Access), used by the folder picker to surface a one-tap grant button.
void SetStorageAccessProvider(std::function<bool()> hasAccess, std::function<void()> requestAccess);

/// True when broad shared-storage access is available (always true on desktop; reflects
/// Android All Files Access when a storage access provider is registered).
[[nodiscard]] bool HasStorageAccess();

/// Opens a modal window for choosing a folder, starting in `start` (or the home folder when it is
/// not a directory). `onPicked` receives the chosen folder; cancelling calls nothing.
void ShowFolderPicker(const std::string& title, const std::filesystem::path& start,
                         std::function<void(const std::filesystem::path&)> onPicked);

/// Opens a modal window for opening an existing UniVex project: lists subfolders (tagging project
/// folders with `[Project]`) alongside `.uvproject` / `.uveditor` files directly inside each
/// folder. `onPicked` receives either the selected `.uvproject` file or the chosen folder.
void ShowProjectPicker(const std::string& title, const std::filesystem::path& start,
                          std::function<void(const std::filesystem::path&)> onPicked);

/// Opens a modal window for choosing one file whose name ends with one of `suffixes`, starting in
/// `start`. Folders are browsed by clicking them; a file is selected by clicking it and chosen with
/// `confirmLabel`. `onPicked` receives the file; cancelling, or confirming with none selected, calls
/// nothing. `onError`, when provided, receives user-facing errors from a system picker.
void ShowFilePicker(const std::string& title, const std::filesystem::path& start, std::vector<std::string> suffixes,
                       const std::string& confirmLabel, std::function<void(const std::filesystem::path&)> onPicked,
                       std::function<void(const std::string&)> onError = {});

} // namespace UVE::LauncherUI
