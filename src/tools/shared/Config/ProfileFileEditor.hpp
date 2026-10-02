// SPDX-License-Identifier: Apache-2.0
//
// Surgical, comment-preserving edits of dbtool.yml.
//
// `ProfileStore::Save` re-serialises the whole document and drops comments and
// formatting. The edits here instead use yaml-cpp only to *locate* nodes, then
// splice new text into the original bytes, so a user's hand-written file keeps
// its comments, ordering, quoting style and line endings.
//
// The `...Text` functions are pure string transforms; `EditConfigFile` applies
// one to a file atomically.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace Lightweight::Config
{

/// Fields written by `AddProfileText`. Empty fields are omitted; every value is
/// emitted as a double-quoted YAML scalar.
struct NewProfile
{
    /// Profile name (the key under `profiles:`).
    std::string name;

    /// Raw ODBC connection string; mutually exclusive with `dsn`.
    std::string connectionString;

    /// ODBC data source name; mutually exclusive with `connectionString`.
    std::string dsn;

    /// User name.
    std::string uid;

    /// Default schema.
    std::string schema;

    /// Plugin directory.
    std::string pluginsDir;

    /// Password exactly as it should be stored — callers pass the encrypted `enc:` value.
    std::string password;
};

/// Whether `AddProfileText` may replace a profile that already exists.
enum class ReplaceExisting : std::uint8_t
{
    No,
    Yes,
};

/// Quotes a value as a YAML double-quoted scalar, escaping `\`, `"` and control characters.
/// @param value Raw value.
/// @return The quoted scalar, e.g. `"a\"b"`.
[[nodiscard]] std::string QuoteYamlScalar(std::string_view value);

/// Replaces the `password` value of one profile, leaving every other byte intact.
/// For a legacy single-profile file the top-level `Password`/`password` is replaced.
/// @param yaml Current file contents.
/// @param profileName Profile whose password to replace.
/// @param newValue New password value (written double-quoted).
/// @return The edited text, or an error (unknown profile, no password, block scalar, parse error).
[[nodiscard]] std::expected<std::string, std::string> SetProfilePasswordText(std::string_view yaml,
                                                                             std::string_view profileName,
                                                                             std::string_view newValue);

/// Inserts a profile as the first entry under `profiles:` (creating the map if needed).
/// @param yaml Current file contents (may be empty).
/// @param profile Profile to add.
/// @param replace Whether an existing profile of the same name may be replaced.
/// @return The edited text, or an error (duplicate name, legacy or flow-style file, parse error).
[[nodiscard]] std::expected<std::string, std::string> AddProfileText(std::string_view yaml,
                                                                     NewProfile const& profile,
                                                                     ReplaceExisting replace);

/// Sets `defaultProfile`, replacing its value or inserting it as the first line.
/// @param yaml Current file contents.
/// @param profileName New default profile name.
/// @return The edited text, or a parse error.
[[nodiscard]] std::expected<std::string, std::string> SetDefaultProfileText(std::string_view yaml,
                                                                            std::string_view profileName);

/// Transformation applied by `EditConfigFile`.
using ConfigTextTransform = std::function<std::expected<std::string, std::string>(std::string_view)>;

/// Reads `path` (an absent file reads as empty), applies `transform` and writes
/// the result atomically (temporary file in the same directory, then rename),
/// creating parent directories as needed. The file is untouched on any failure.
/// @param path Configuration file.
/// @param transform Edit to apply.
/// @return Nothing on success, or the transform's / an I/O error (e.g. read-only file).
[[nodiscard]] std::expected<void, std::string> EditConfigFile(std::filesystem::path const& path,
                                                              ConfigTextTransform const& transform);

} // namespace Lightweight::Config
