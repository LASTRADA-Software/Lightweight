// SPDX-License-Identifier: Apache-2.0
//
// `dbtool new-migration`: scaffolds an empty, timestamped migration source file.
//
// Everything except WriteNewMigrationFile() is pure, and the clock is a parameter throughout, so the
// timestamp and the rendered source can be tested without waiting for a particular second.

#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Lightweight::Tools
{

/// The name and content of a migration source file about to be created.
struct NewMigrationFile
{
    /// The file name, `<YYYYMMDDHHMMSS>_<slug>.cpp`, or `<YYYYMMDDHHMMSS>.cpp` for a title without ASCII letters or digits.
    std::string fileName;
    /// The complete C++ source of the file.
    std::string source;
};

/// Reads the timestamp a migration file name starts with.
/// @param fileName A file name without a directory, e.g. `20260126120000_create_users.cpp`.
/// @return The UTC time point, or `std::nullopt` unless the name starts with exactly 14 digits forming a valid date
///         and time.
[[nodiscard]] std::optional<std::chrono::sys_seconds> ParseMigrationFileTimestamp(std::string_view fileName);

/// Picks the timestamp of a new migration: the current time, moved past every migration that already exists, since
/// migrations must be unique and ordered by timestamp.
/// @param now The current UTC time.
/// @param existing The timestamps of the migrations already in the target directory.
/// @return @p now, or one second after the latest of @p existing when that is not earlier than @p now.
[[nodiscard]] std::chrono::sys_seconds NextMigrationTimestamp(std::chrono::sys_seconds now,
                                                              std::span<std::chrono::sys_seconds const> existing) noexcept;

/// Renders the migration source file for @p title.
/// @param title The human-readable migration title, stored as the migration's description.
/// @param timestamp The UTC timestamp of the migration.
/// @return The file to create, or a message when @p title is blank or holds a control character.
[[nodiscard]] std::expected<NewMigrationFile, std::string> MakeNewMigrationFile(std::string_view title,
                                                                                std::chrono::sys_seconds timestamp);

/// Creates a new migration source file in @p directory.
/// @param title The human-readable migration title.
/// @param directory The existing directory to create the file in; its `<timestamp>*` files decide the timestamp.
/// @param now The current UTC time.
/// @return The path of the new file, or a message when the title is invalid or the file cannot be written.
[[nodiscard]] std::expected<std::filesystem::path, std::string> WriteNewMigrationFile(std::string_view title,
                                                                                      std::filesystem::path const& directory,
                                                                                      std::chrono::sys_seconds now);

} // namespace Lightweight::Tools
