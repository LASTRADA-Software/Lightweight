// SPDX-License-Identifier: Apache-2.0
//
// Pure decision logic for the managed backup feature — no Qt types, no
// I/O beyond std::filesystem, so every branch is unit-testable without an
// event loop. The Qt-facing orchestration lives in ManagedBackupController.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <Config/ProfileStore.hpp>
#include <Secrets/SecretResolver.hpp>

namespace DbtoolGui::ManagedBackup
{

/// One profile's archive-name assignment inside a backup-all run.
struct ArchivePlanEntry
{
    /// Profile name exactly as configured.
    std::string profileName;
    /// Sanitized on-disk file name (`<sanitized>.zip`).
    std::string fileName;
    /// True when an earlier profile already claimed `fileName`; collided
    /// profiles are skipped with an error instead of silently merged.
    bool collision = false;
};

/// Result of probing one archive file on disk.
struct ArchiveStatus
{
    /// True when the archive file exists.
    bool exists = false;
    /// File size in bytes (0 when missing).
    std::uintmax_t sizeBytes = 0;
    /// Last modification time (default-constructed when missing).
    std::filesystem::file_time_type mtime;
};

/// Renders a path as UTF-8 for a user-facing message.
///
/// `std::filesystem::path::string()` re-encodes into the process' active code
/// page on Windows, which mangles (and can outright fail on) a non-ASCII backup
/// folder such as `C:\Users\Müller\Sicherungen`. The UTF-8 form round-trips
/// through the GUI unchanged.
/// @param value Path to render.
/// @return The path encoded as UTF-8.
[[nodiscard]] std::string PathToUtf8(std::filesystem::path const& value);

/// Maps a profile name onto a filesystem-safe base name: characters outside
/// `[A-Za-z0-9_-]` become `_`; an empty name becomes `"_"`.
/// @param profileName Profile name as configured in dbtool.yml.
/// @return Sanitized base name without extension.
[[nodiscard]] std::string SanitizeArchiveName(std::string_view profileName);

/// Assigns `<sanitized>.zip` file names to every profile, flagging later
/// entries whose sanitized name collides with an earlier one.
/// @param profileNames Profile names in store order.
/// @return One entry per input, same order.
[[nodiscard]] std::vector<ArchivePlanEntry> PlanArchiveNames(std::vector<std::string> const& profileNames);

/// Probes one archive path on disk.
/// @param archivePath Full path to the `.zip`.
/// @return exists/size/mtime; missing files yield `{false, 0, {}}`.
[[nodiscard]] ArchiveStatus ScanArchive(std::filesystem::path const& archivePath);

/// The temporary sibling written before an atomic replace.
/// @param finalPath Final archive path.
/// @return `finalPath` with `.tmp` appended.
[[nodiscard]] std::filesystem::path TempArchivePath(std::filesystem::path const& finalPath);

/// Atomically renames `tmpPath` over `finalPath` (same directory, so the
/// rename is atomic on POSIX and Windows).
/// @param tmpPath Fully-written temporary archive.
/// @param finalPath Destination archive path.
/// @return Nothing on success, an error message on failure.
[[nodiscard]] std::expected<void, std::string> CommitArchive(std::filesystem::path const& tmpPath,
                                                             std::filesystem::path const& finalPath);

/// Ensures `folder` exists (creating it if needed) and is a writable
/// directory, verified by creating and removing a probe file.
/// @param folder Backup folder to validate.
/// @return Nothing on success, a user-displayable error message otherwise.
[[nodiscard]] std::expected<void, std::string> CheckWritableFolder(std::filesystem::path const& folder);

/// Human-readable one-line summary for a finished backup-all run.
/// @param okCount Number of successfully backed-up profiles.
/// @param failCount Number of failed/skipped profiles.
/// @return E.g. `"Backed up 2 profile(s), 1 failed."`.
[[nodiscard]] std::string FormatRunSummary(int okCount, int failCount);

/// Resolves a profile to a ready-to-use ODBC connection string via
/// `Profile::ToConnectInfo`: the password resolved from `secretRef` (when
/// set) through `resolver` is appended to a raw `connectionString` (unless
/// it already carries `PWD=`/`Password=`), or folded into a DSN descriptor
/// built from `dsn`/`uid` otherwise. A raw `connectionString` with no
/// `secretRef` is returned unmodified. Unlike AppController::connectToProfile
/// (which never resolves `secretRef`), this always resolves the profile's
/// secret when one is set and fails with an error message when it cannot —
/// managed backups run headless, so an unresolved password must abort rather
/// than silently connect without one.
/// @param profile Profile to resolve.
/// @param resolver Secret resolver chain (see MakeDefaultResolver()).
/// @return Connection string, or a user-displayable error message.
[[nodiscard]] std::expected<std::string, std::string> ResolveConnectionString(
    Lightweight::Config::Profile const& profile, Lightweight::Secrets::SecretResolver const& resolver);

} // namespace DbtoolGui::ManagedBackup
