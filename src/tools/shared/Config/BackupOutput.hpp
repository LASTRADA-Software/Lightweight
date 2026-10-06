// SPDX-License-Identifier: Apache-2.0
//
// Where `dbtool backup` writes its archive when `dbtool.yml` names a backup folder
// (`defaultBackupDir` / a profile's `backupDir`).
//
// Kept free of any I/O, clock or environment access so it can be tested exhaustively:
// the caller passes the time, and creating the folder is the caller's job.

#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Lightweight::Config
{

/// Inputs of `ResolveBackupOutput`.
struct BackupOutputRequest
{
    /// `--output` as given on the command line; empty when omitted.
    std::filesystem::path requested;

    /// The configured backup folder (`EffectiveBackupDir`); empty when none is configured.
    std::filesystem::path folder;

    /// Name of the selected profile, used for a generated file name; may be empty.
    std::string_view profileName;

    /// The moment the backup starts, used for a generated file name.
    std::chrono::system_clock::time_point now;
};

/// The archive path to write, and whether it was placed in the configured folder.
struct BackupOutput
{
    /// File to write.
    std::filesystem::path path;

    /// True when `path` lies in the configured folder because of the folder (a bare file name,
    /// or a generated one) — the caller should create that folder if it is missing. False when
    /// the user's own path was kept as given.
    bool inConfiguredFolder = false;
};

/// File name `dbtool backup` generates when `--output` is omitted: `<profile>-<YYYYMMDD-HHMMSS>.zip`
/// in local time, with characters that are unsafe in a file name replaced by `_`
/// (`backup-<…>.zip` when there is no profile name).
/// @param profileName Selected profile's name, may be empty.
/// @param now The moment the backup starts.
/// @return The bare file name.
[[nodiscard]] std::string GeneratedBackupFileName(std::string_view profileName, std::chrono::system_clock::time_point now);

/// Decides where a backup is written.
///
/// - A path with a directory part (`D:/x/a.zip`, `./a.zip`, `sub/a.zip`) stays as given.
/// - A bare file name (`a.zip`) goes into the configured folder, when there is one.
/// - No `--output` goes into the configured folder under a generated name, when there is one.
/// - With no configured folder the request is returned unchanged (and is empty when nothing was requested).
///
/// @param request What the user asked for and what is configured.
/// @return The resolved output, or `std::nullopt` when there is nothing to write to (no
///         `--output` and no configured folder).
[[nodiscard]] std::optional<BackupOutput> ResolveBackupOutput(BackupOutputRequest const& request);

} // namespace Lightweight::Config
