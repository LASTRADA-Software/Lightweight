// SPDX-License-Identifier: Apache-2.0

#include "LupSqlParser.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <regex>

namespace Lup2DbTool
{

namespace
{

    // Windows-1252 to UTF-8 conversion table for characters 0x80-0x9F
    // These are the special characters in Windows-1252 that differ from Latin-1
    constexpr std::array<char const*, 32> kWindows1252ToUtf8 = {
        "\xE2\x82\xAC", // 0x80 Euro sign
        "\xEF\xBF\xBD", // 0x81 Undefined (replacement char)
        "\xE2\x80\x9A", // 0x82 Single low-9 quotation mark
        "\xC6\x92",     // 0x83 Latin small letter f with hook
        "\xE2\x80\x9E", // 0x84 Double low-9 quotation mark
        "\xE2\x80\xA6", // 0x85 Horizontal ellipsis
        "\xE2\x80\xA0", // 0x86 Dagger
        "\xE2\x80\xA1", // 0x87 Double dagger
        "\xCB\x86",     // 0x88 Modifier letter circumflex accent
        "\xE2\x80\xB0", // 0x89 Per mille sign
        "\xC5\xA0",     // 0x8A Latin capital letter S with caron
        "\xE2\x80\xB9", // 0x8B Single left-pointing angle quotation mark
        "\xC5\x92",     // 0x8C Latin capital ligature OE
        "\xEF\xBF\xBD", // 0x8D Undefined (replacement char)
        "\xC5\xBD",     // 0x8E Latin capital letter Z with caron
        "\xEF\xBF\xBD", // 0x8F Undefined (replacement char)
        "\xEF\xBF\xBD", // 0x90 Undefined (replacement char)
        "\xE2\x80\x98", // 0x91 Left single quotation mark
        "\xE2\x80\x99", // 0x92 Right single quotation mark
        "\xE2\x80\x9C", // 0x93 Left double quotation mark
        "\xE2\x80\x9D", // 0x94 Right double quotation mark
        "\xE2\x80\xA2", // 0x95 Bullet
        "\xE2\x80\x93", // 0x96 En dash
        "\xE2\x80\x94", // 0x97 Em dash
        "\xCB\x9C",     // 0x98 Small tilde
        "\xE2\x84\xA2", // 0x99 Trade mark sign
        "\xC5\xA1",     // 0x9A Latin small letter s with caron
        "\xE2\x80\xBA", // 0x9B Single right-pointing angle quotation mark
        "\xC5\x93",     // 0x9C Latin small ligature oe
        "\xEF\xBF\xBD", // 0x9D Undefined (replacement char)
        "\xC5\xBE",     // 0x9E Latin small letter z with caron
        "\xC5\xB8",     // 0x9F Latin capital letter Y with diaeresis
    };

    std::string Trim(std::string_view str)
    {
        auto start = str.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos)
            return {};
        auto end = str.find_last_not_of(" \t\r\n");
        return std::string(str.substr(start, end - start + 1));
    }

    std::optional<LupVersion> ParseBaseVersionDirective(std::string_view comment)
    {
        // Pattern: --[Based on Lup Version X.X.X]
        std::regex pattern(R"(\[Based\s+on\s+Lup\s+Version\s+(\d+)\.(\d+)\.(\d+)\])", std::regex::icase);
        std::string str(comment);
        std::smatch match;
        if (std::regex_search(str, match, pattern))
        {
            LupVersion version;
            version.major = std::stoi(match[1].str());
            version.minor = std::stoi(match[2].str());
            version.patch = std::stoi(match[3].str());
            return version;
        }
        return std::nullopt;
    }

    std::optional<std::string> ParsePrintDirective(std::string_view comment)
    {
        // Pattern: --print 'message'
        std::regex pattern(R"(print\s+'([^']*)')", std::regex::icase);
        std::string str(comment);
        std::smatch match;
        if (std::regex_search(str, match, pattern))
            return match[1].str();
        return std::nullopt;
    }

    bool IsComment(std::string_view line)
    {
        auto trimmed = Trim(line);
        return trimmed.starts_with("--");
    }

    bool IsEmptyOrWhitespace(std::string_view line)
    {
        return Trim(line).empty();
    }

} // namespace

std::string ConvertWindows1252ToUtf8(std::string_view input)
{
    std::string output;
    output.reserve(input.size() * 2); // Reserve space for potential UTF-8 expansion

    for (char ch: input)
    {
        auto c = static_cast<unsigned char>(ch);
        if (c < 0x80)
        {
            // ASCII range - direct copy
            output += static_cast<char>(c);
        }
        else if (c >= 0x80 && c <= 0x9F)
        {
            // Windows-1252 special range
            output += kWindows1252ToUtf8[c - 0x80];
        }
        else
        {
            // Latin-1 supplement (0xA0-0xFF) - convert to UTF-8
            output += static_cast<char>(0xC0 | (c >> 6));
            output += static_cast<char>(0x80 | (c & 0x3F));
        }
    }

    return output;
}

namespace
{

    /// @brief Removes C-style multi-line comments (/* ... */) from the content.
    /// Also extracts comment text for directive processing.
    std::string StripMultiLineComments(std::string_view content, std::vector<std::string>& extractedComments)
    {
        std::string result;
        result.reserve(content.size());

        size_t i = 0;
        while (i < content.size())
        {
            // Check for start of multi-line comment
            if (i + 1 < content.size() && content[i] == '/' && content[i + 1] == '*')
            {
                // Find the end of the comment
                size_t commentStart = i + 2;
                size_t commentEnd = content.find("*/", commentStart);
                if (commentEnd != std::string_view::npos)
                {
                    // Extract comment text for directive processing
                    auto commentText = Trim(content.substr(commentStart, commentEnd - commentStart));
                    if (!commentText.empty())
                        extractedComments.emplace_back(commentText);

                    // Preserve newlines within the comment to maintain line structure
                    for (size_t j = i; j < commentEnd + 2; ++j)
                    {
                        if (content[j] == '\n')
                            result += '\n';
                    }
                    i = commentEnd + 2;
                }
                else
                {
                    // Unclosed comment - treat rest as comment
                    break;
                }
            }
            else
            {
                result += content[i];
                ++i;
            }
        }

        return result;
    }

    /// @brief Checks if a line starts with a SQL keyword (case-insensitive).
    bool StartsWithSqlKeyword(std::string_view trimmed)
    {
        // List of SQL keywords that can start a statement
        static constexpr std::array<std::string_view, 6> keywords = {
            "CREATE", "ALTER", "INSERT", "UPDATE", "DELETE", "DROP"
        };

        for (auto const& kw: keywords)
        {
            if (trimmed.size() >= kw.size())
            {
                bool matches = true;
                for (size_t i = 0; i < kw.size() && matches; ++i)
                {
                    matches = (std::toupper(static_cast<unsigned char>(trimmed[i]))
                               == static_cast<unsigned char>(kw[i]));
                }
                // Ensure it's followed by whitespace or end of string
                if (matches && (trimmed.size() == kw.size() || std::isspace(static_cast<unsigned char>(trimmed[kw.size()]))))
                    return true;
            }
        }
        return false;
    }

    /// @brief Counts the parentheses balance in a string.
    /// Returns: positive if more '(' than ')', negative if more ')' than '(', zero if balanced.
    int CountParenthesesBalance(std::string_view str)
    {
        int balance = 0;
        bool inString = false;
        char stringChar = 0;

        for (size_t i = 0; i < str.size(); ++i)
        {
            char c = str[i];

            // Handle string literals
            if (!inString && (c == '\'' || c == '"'))
            {
                inString = true;
                stringChar = c;
            }
            else if (inString && c == stringChar)
            {
                // Check for escaped quote
                if (i + 1 < str.size() && str[i + 1] == stringChar)
                    ++i; // Skip escaped quote
                else
                    inString = false;
            }
            else if (!inString)
            {
                if (c == '(')
                    ++balance;
                else if (c == ')')
                    --balance;
            }
        }
        return balance;
    }

    /// @brief Strips inline comments (-- ...) from a line, preserving the SQL before it.
    std::string StripInlineComment(std::string_view line)
    {
        // Find -- that's not inside a string literal
        bool inString = false;
        char stringChar = 0;

        for (size_t i = 0; i < line.size(); ++i)
        {
            char c = line[i];

            if (!inString && (c == '\'' || c == '"'))
            {
                inString = true;
                stringChar = c;
            }
            else if (inString && c == stringChar)
            {
                if (i + 1 < line.size() && line[i + 1] == stringChar)
                    ++i; // Skip escaped quote
                else
                    inString = false;
            }
            else if (!inString && c == '-' && i + 1 < line.size() && line[i + 1] == '-')
            {
                // Found inline comment
                return Trim(line.substr(0, i));
            }
        }
        return std::string(line);
    }

    struct ParseContext
    {
        ParsedMigration& migration;
        std::vector<std::string> currentComments;
        std::string currentStatement;
        int parenBalance = 0; // Track parentheses for multi-line statements

        void FinalizeStatement()
        {
            if (!currentStatement.empty())
            {
                auto parsed = ParseSqlStatement(currentStatement);
                migration.statements.push_back({ std::move(currentComments), std::move(parsed) });
                currentComments.clear();
                currentStatement.clear();
                parenBalance = 0;
            }
        }

        void ProcessDirective(std::string_view commentText)
        {
            if (auto baseVer = ParseBaseVersionDirective(commentText))
                migration.baseVersion = baseVer;
            else if (auto printMsg = ParsePrintDirective(commentText))
            {
                if (migration.title.empty())
                    migration.title = *printMsg;
            }
        }

        void ProcessLine(std::string_view line)
        {
            auto trimmed = Trim(line);

            if (IsEmptyOrWhitespace(line))
            {
                // Only finalize if parentheses are balanced
                if (parenBalance == 0)
                    FinalizeStatement();
                return;
            }

            if (IsComment(line))
            {
                auto commentText = Trim(trimmed.substr(2));
                ProcessDirective(commentText);
                if (!commentText.empty())
                    currentComments.emplace_back(commentText);
                return;
            }

            // Strip inline comments from SQL
            auto sqlPart = StripInlineComment(trimmed);
            if (sqlPart.empty())
                return;

            // Check if this line starts a new SQL statement
            if (StartsWithSqlKeyword(sqlPart))
            {
                // If we have a pending statement with balanced parentheses, finalize it first
                if (!currentStatement.empty() && parenBalance == 0)
                    FinalizeStatement();
            }

            // Append to current statement
            if (!currentStatement.empty())
                currentStatement += " ";
            currentStatement += sqlPart;

            // Update parentheses balance
            parenBalance += CountParenthesesBalance(sqlPart);
        }
    };

} // namespace

std::optional<ParsedMigration> ParseSqlFile(std::filesystem::path const& filePath, ParserConfig const& config)
{
    // Read the file
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Convert encoding if needed
    if (config.inputEncoding == "windows-1252")
        content = ConvertWindows1252ToUtf8(content);

    // Parse filename for version
    auto versionOpt = ParseFilename(filePath.filename().string());
    if (!versionOpt)
        return std::nullopt;

    ParsedMigration migration;
    migration.sourceFile = filePath;
    migration.targetVersion = *versionOpt;

    // Strip multi-line comments and extract their content for directive processing
    std::vector<std::string> multiLineComments;
    content = StripMultiLineComments(content, multiLineComments);

    ParseContext ctx { .migration = migration, .currentComments = {}, .currentStatement = {} };

    // Process directives from multi-line comments
    for (auto const& comment: multiLineComments)
        ctx.ProcessDirective(comment);

    // Split content by lines and process
    size_t pos = 0;
    size_t prevPos = 0;
    while ((pos = content.find('\n', prevPos)) != std::string::npos)
    {
        auto line = std::string_view(content).substr(prevPos, pos - prevPos);
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r')
            line = line.substr(0, line.size() - 1);
        ctx.ProcessLine(line);
        prevPos = pos + 1;
    }
    // Process last line if no trailing newline
    if (prevPos < content.size())
        ctx.ProcessLine(std::string_view(content).substr(prevPos));

    // Finalize any remaining statement
    ctx.FinalizeStatement();

    // Generate default title if none found
    if (migration.title.empty())
    {
        if (IsInitMigration(filePath.filename().string()))
            migration.title = "Initial schema " + migration.targetVersion.ToString();
        else
            migration.title = "LUP Update " + migration.targetVersion.ToString();
    }

    return migration;
}

std::vector<std::filesystem::path> DiscoverMigrationFiles(std::filesystem::path const& directory)
{
    std::vector<std::pair<LupVersion, std::filesystem::path>> filesWithVersions;

    for (auto const& entry: std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
            continue;

        auto const& path = entry.path();
        auto filename = path.filename().string();

        // Check if it's a LUP migration file
        if (!filename.ends_with(".sql"))
            continue;

        if (!IsInitMigration(filename) && !IsUpdateMigration(filename))
            continue;

        // Parse version from filename
        if (auto version = ParseFilename(filename))
            filesWithVersions.emplace_back(*version, path);
    }

    // Sort by version
    std::ranges::sort(filesWithVersions, [](auto const& a, auto const& b) { return a.first < b.first; });

    // Extract just the paths
    std::vector<std::filesystem::path> result;
    result.reserve(filesWithVersions.size());
    for (auto const& [_, path]: filesWithVersions)
        result.push_back(path);

    return result;
}

} // namespace Lup2DbTool
