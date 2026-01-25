// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "LupVersionConverter.hpp"
#include "SqlStatementParser.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Lup2DbTool
{

/// @brief Represents a parsed comment from the SQL file.
struct Comment
{
    std::string text;
    bool isDirective = false; // true if this is a special directive like --[Based on Lup Version X.X.X]
};

/// @brief Represents a parsed SQL migration file.
struct ParsedMigration
{
    std::filesystem::path sourceFile;

    /// Target version of this migration
    LupVersion targetVersion;

    /// Base version this migration requires (from --[Based on Lup Version X.X.X])
    std::optional<LupVersion> baseVersion;

    /// The migration title/description (from --print 'message')
    std::string title;

    /// All parsed SQL statements with their preceding comments
    struct StatementWithComments
    {
        std::vector<std::string> comments;
        ParsedStatement statement;
    };
    std::vector<StatementWithComments> statements;
};

/// @brief Configuration for the SQL parser.
struct ParserConfig
{
    /// Input encoding: "windows-1252" or "utf-8"
    std::string inputEncoding = "windows-1252";
};

/// @brief Parses a LUP SQL migration file.
///
/// This parser handles:
/// - Encoding conversion from Windows-1252 to UTF-8
/// - Directive parsing (--[Based on Lup Version X.X.X], --/* LUP-Version: X_X_X */, --print 'message')
/// - SQL statement extraction (newline-delimited, no semicolons)
/// - Comment preservation for transfer to C++ output
///
/// @param filePath Path to the SQL file
/// @param config Parser configuration
/// @return Parsed migration structure or nullopt on error
[[nodiscard]] std::optional<ParsedMigration> ParseSqlFile(std::filesystem::path const& filePath, ParserConfig const& config);

/// @brief Converts a string from Windows-1252 encoding to UTF-8.
[[nodiscard]] std::string ConvertWindows1252ToUtf8(std::string_view input);

/// @brief Discovers all LUP migration files in a directory.
///
/// Files are sorted by version number to ensure correct migration order.
///
/// @param directory Path to the directory containing SQL files
/// @return Vector of paths to discovered migration files, sorted by version
[[nodiscard]] std::vector<std::filesystem::path> DiscoverMigrationFiles(std::filesystem::path const& directory);

} // namespace Lup2DbTool
