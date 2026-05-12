// SPDX-License-Identifier: Apache-2.0

#include "LupSqlParser.hpp"
#include "StringUtils.hpp"

#include <Lightweight/DataBinder/UnicodeConverter.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <fstream>
#include <print>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>

namespace Lup2DbTool
{

namespace
{

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
                    matches = (std::toupper(static_cast<unsigned char>(trimmed[i])) == static_cast<unsigned char>(kw[i]));
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
                migration.statements.push_back({ .comments = std::move(currentComments), .statement = std::move(parsed) });
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

    /// @brief Builds the byte buffer used as the *encoding-detection signal* for a SQL file.
    ///
    /// LUP `.sql` files frequently carry author notes, change-logs, and TODOs in mixed
    /// encodings inside SQL comments. Those bytes are irrelevant for choosing how to
    /// decode the actual statements — they get carried along with whatever encoding the
    /// payload dictates. To keep detection robust the signal excludes:
    ///   - `/* ... */` block comments (via `StripMultiLineComments`),
    ///   - any line whose first non-whitespace characters are `--`.
    ///
    /// The returned buffer is *only* used to inspect bytes; the real decoding step
    /// always operates on the original file contents.
    std::string StripCommentsForEncodingDetection(std::string_view content)
    {
        std::vector<std::string> discarded;
        auto withoutBlockComments = StripMultiLineComments(content, discarded);

        std::string result;
        result.reserve(withoutBlockComments.size());

        size_t prevPos = 0;
        size_t pos = 0;
        while ((pos = withoutBlockComments.find('\n', prevPos)) != std::string::npos)
        {
            auto line = std::string_view(withoutBlockComments).substr(prevPos, pos - prevPos);
            if (!IsComment(line))
                result.append(line.data(), line.size());
            result.push_back('\n');
            prevPos = pos + 1;
        }
        if (prevPos < withoutBlockComments.size())
        {
            auto line = std::string_view(withoutBlockComments).substr(prevPos);
            if (!IsComment(line))
                result.append(line.data(), line.size());
        }
        return result;
    }

    struct Utf8ValidationResult
    {
        bool valid = false;
        bool sawNonAscii = false;
        size_t errorOffset = 0; ///< Byte offset of the first invalid byte when !valid.
        unsigned char errorByte = 0;
    };

    /// @brief Walks @p bytes and verifies it is a sequence of well-formed UTF-8 code units.
    ///
    /// Pure ASCII (every byte < 0x80) trivially validates with `sawNonAscii == false`. Any
    /// multi-byte lead must be followed by the right number of continuation bytes
    /// (0b10xxxxxx). Overlong encodings, surrogates, and out-of-range code points are
    /// rejected with the byte offset of the first violation so callers can produce a
    /// useful error message.
    Utf8ValidationResult ValidateUtf8(std::string_view bytes) noexcept
    {
        Utf8ValidationResult result {};
        size_t i = 0;
        while (i < bytes.size())
        {
            auto const c = static_cast<unsigned char>(bytes[i]);
            if (c < 0x80)
            {
                ++i;
                continue;
            }

            result.sawNonAscii = true;

            int extra = 0;
            char32_t codepoint = 0;
            unsigned char minLead = 0;
            if ((c & 0b1110'0000) == 0b1100'0000)
            {
                extra = 1;
                codepoint = c & 0b0001'1111;
                minLead = 0xC2; // 0xC0/0xC1 would be overlong.
            }
            else if ((c & 0b1111'0000) == 0b1110'0000)
            {
                extra = 2;
                codepoint = c & 0b0000'1111;
                minLead = 0xE0;
            }
            else if ((c & 0b1111'1000) == 0b1111'0000)
            {
                extra = 3;
                codepoint = c & 0b0000'0111;
                minLead = 0xF0;
            }
            else
            {
                result.errorOffset = i;
                result.errorByte = c;
                return result;
            }

            if (c < minLead)
            {
                result.errorOffset = i;
                result.errorByte = c;
                return result;
            }

            if (i + static_cast<size_t>(extra) >= bytes.size())
            {
                result.errorOffset = i;
                result.errorByte = c;
                return result;
            }

            auto const extraBytes = static_cast<size_t>(extra);
            for (size_t k = 1; k <= extraBytes; ++k)
            {
                auto const cont = static_cast<unsigned char>(bytes[i + k]);
                if ((cont & 0b1100'0000) != 0b1000'0000)
                {
                    result.errorOffset = i + k;
                    result.errorByte = cont;
                    return result;
                }
                codepoint = (codepoint << 6) | (cont & 0b0011'1111);
            }

            // Reject overlong / surrogate / out-of-range code points.
            bool const overlong = (extra == 2 && codepoint < 0x800) || (extra == 3 && codepoint < 0x10000);
            bool const surrogate = (codepoint >= 0xD800 && codepoint <= 0xDFFF);
            bool const tooLarge = codepoint > 0x10FFFF;
            if (overlong || surrogate || tooLarge)
            {
                result.errorOffset = i;
                result.errorByte = c;
                return result;
            }

            i += static_cast<size_t>(extra) + 1;
        }

        result.valid = true;
        return result;
    }

    /// @brief Whether @p byte is one of the bytes that Windows-1252 leaves undefined
    /// (`0x81`, `0x8D`, `0x8F`, `0x90`, `0x9D`).
    ///
    /// Files containing those bytes are not actually valid Windows-1252; the
    /// `ConvertWindows1252ToUtf8` helper maps them to U+FFFD, but the caller deserves a
    /// warning so the source can be inspected.
    bool IsWindows1252UndefinedByte(unsigned char byte) noexcept
    {
        return byte == 0x81 || byte == 0x8D || byte == 0x8F || byte == 0x90 || byte == 0x9D;
    }

    /// @brief Result of the encoding-detection step.
    struct DecodedContent
    {
        std::string utf8;    ///< File contents as valid UTF-8.
        std::string warning; ///< Optional non-fatal warning surfaced to the caller.
    };

    /// @brief Resolves the requested encoding mode against the actual bytes of @p original.
    ///
    /// `mode` accepts the same vocabulary as `--input-encoding`:
    ///   - `auto`         — pick UTF-8 if the comment-stripped signal validates as UTF-8,
    ///                      otherwise fall back to Windows-1252.
    ///   - `utf-8`        — require the comment-stripped signal to be valid UTF-8.
    ///   - `windows-1252` — require the comment-stripped signal to NOT be valid UTF-8 with
    ///                      non-ASCII content (a UTF-8 file mislabeled as W-1252 is the
    ///                      common mistake; we refuse it instead of double-encoding).
    ///
    /// On a mismatch the function returns `std::unexpected` with a message that names the
    /// file, the offending byte, and its offset in the original buffer.
    std::expected<DecodedContent, std::string> DetectAndDecode(std::string const& original,
                                                               std::string_view mode,
                                                               std::filesystem::path const& path)
    {
        // Two validation passes are needed:
        //   - `signalValidation` answers "what encoding is the SQL payload in?" — used for
        //     classification and (in explicit modes) for the user-facing error message.
        //   - `fullValidation` answers "are the original bytes already valid UTF-8 end-to-end?"
        //     — used in auto mode to decide whether *any* conversion is required, so a file
        //     whose payload is pure ASCII but whose comments contain raw W-1252 bytes still
        //     gets converted (otherwise the C++ compiler rejects the embedded comment text).
        auto const signal = StripCommentsForEncodingDetection(original);
        auto const signalValidation = ValidateUtf8(signal);
        auto const fullValidation = ValidateUtf8(original);

        auto const convertFromWindows1252 = [&](std::string warning = {}) -> DecodedContent {
            auto const u8 = Lightweight::ConvertWindows1252ToUtf8(original);
            DecodedContent decoded;
            decoded.utf8.assign(u8.begin(), u8.end());
            decoded.warning = std::move(warning);
            return decoded;
        };

        auto const checkForUndefinedBytes = [&]() -> std::string {
            for (size_t i = 0; i < original.size(); ++i)
            {
                auto const byte = static_cast<unsigned char>(original[i]);
                if (IsWindows1252UndefinedByte(byte))
                {
                    return std::format("'{}': contains byte 0x{:02X} at offset {} which is undefined in "
                                       "Windows-1252; it will be mapped to U+FFFD",
                                       path.string(),
                                       static_cast<unsigned>(byte),
                                       i);
                }
            }
            return {};
        };

        if (mode == "auto")
        {
            if (fullValidation.valid)
                return DecodedContent { .utf8 = original, .warning = {} };
            return convertFromWindows1252(checkForUndefinedBytes());
        }

        if (mode == "utf-8")
        {
            // Comments are excluded from the validation signal: legacy LUP files frequently
            // carry author notes or change-logs in mixed encodings inside `--` / `/* … */`
            // banners. We do not want to reject an otherwise UTF-8 file because its banner
            // happens to contain a Latin-1 umlaut.
            if (signalValidation.valid)
                return DecodedContent { .utf8 = original, .warning = {} };
            return std::unexpected(
                std::format("'{}': declared as utf-8 but contains invalid UTF-8 byte 0x{:02X} at offset {} "
                            "(outside comments)",
                            path.string(),
                            static_cast<unsigned>(signalValidation.errorByte),
                            signalValidation.errorOffset));
        }

        if (mode == "windows-1252")
        {
            // A non-ASCII payload that successfully validates as UTF-8 almost certainly means
            // the file was mislabeled — running it through the W-1252 converter would
            // double-encode every multi-byte sequence (the original incident this work
            // addresses). Refuse loudly so the build fails instead of silently mangling.
            if (signalValidation.valid && signalValidation.sawNonAscii)
            {
                return std::unexpected(std::format("'{}': declared as windows-1252 but the SQL payload validates as UTF-8 "
                                                   "(would double-encode); rerun with --input-encoding=auto or =utf-8",
                                                   path.string()));
            }
            return convertFromWindows1252(checkForUndefinedBytes());
        }

        return std::unexpected(
            std::format("'{}': unknown input encoding '{}' (expected auto, utf-8, or windows-1252)", path.string(), mode));
    }

} // namespace

std::expected<ParsedMigration, std::string> ParseSqlFile(std::filesystem::path const& filePath, ParserConfig const& config)
{
    // Read the file
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        return std::unexpected(std::format("'{}': failed to open file", filePath.string()));

    std::string original((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Resolve the encoding (auto-detect or validate the explicit declaration).
    auto decoded = DetectAndDecode(original, config.inputEncoding, filePath);
    if (!decoded)
        return std::unexpected(std::move(decoded.error()));
    if (!decoded->warning.empty())
        std::println(stderr, "Warning: {}", decoded->warning);

    auto content = std::move(decoded->utf8);

    // Parse filename for version
    auto versionOpt = ParseFilename(filePath.filename().string());
    if (!versionOpt)
        return std::unexpected(
            std::format("'{}': filename does not match LUP migration pattern", filePath.filename().string()));

    // Inform the user when a development placeholder file is picked up under the sentinel version.
    if (*versionOpt == LupVersion { .major = 9999, .minor = 99, .patch = 99 })
    {
        std::println(stderr,
                     "Info: '{}' assigned development sentinel version {}.",
                     filePath.filename().string(),
                     versionOpt->ToString());
    }

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

namespace
{

    using SchemaMap = std::unordered_map<std::string, std::vector<std::string>>;

    /// True iff `name` consists entirely of decimal digits — the marker
    /// `ParseInsert` uses when the source `INSERT … VALUES` had no explicit
    /// column list and positional indices were synthesized in its place.
    bool IsPositionalColumnName(std::string_view name)
    {
        if (name.empty())
            return false;
        return std::ranges::all_of(name, [](char c) { return c >= '0' && c <= '9'; });
    }

    bool HasAnyPositionalColumn(InsertStmt const& stmt)
    {
        return std::ranges::any_of(stmt.columnValues, [](auto const& kv) { return IsPositionalColumnName(kv.first); });
    }

    void RecordCreateTable(SchemaMap& schema, CreateTableStmt const& s)
    {
        auto& cols = schema[s.tableName];
        cols.clear();
        cols.reserve(s.columns.size());
        for (auto const& col: s.columns)
            cols.push_back(col.name);
    }

    void RewriteInsertColumns(InsertStmt& s, std::vector<std::string> const& trackedCols, std::string_view sourceFilename)
    {
        for (auto& [name, _]: s.columnValues)
        {
            if (!IsPositionalColumnName(name))
                continue;
            auto const idx = static_cast<size_t>(std::stoul(name));
            if (idx >= trackedCols.size())
            {
                std::println(stderr,
                             "warn: INSERT into '{}' in {}: positional index {} "
                             "exceeds tracked schema ({} columns) — likely a "
                             "comma-in-string parser artifact; leaving untouched",
                             s.tableName,
                             sourceFilename,
                             idx,
                             trackedCols.size());
                continue;
            }
            name = trackedCols[idx];
        }
    }

    void ResolveInsertStatement(SchemaMap const& schema, InsertStmt& s, std::string_view sourceFilename)
    {
        if (s.columnValues.empty() || !HasAnyPositionalColumn(s))
            return;

        auto const it = schema.find(s.tableName);
        if (it == schema.end())
        {
            std::println(stderr,
                         "warn: INSERT into unknown table '{}' in {} — "
                         "cannot resolve positional columns",
                         s.tableName,
                         sourceFilename);
            return;
        }
        RewriteInsertColumns(s, it->second, sourceFilename);
    }

    void ApplyStatementToSchema(SchemaMap& schema, ParsedStatement& stmt, std::string_view sourceFilename)
    {
        std::visit(
            [&](auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, CreateTableStmt>)
                    RecordCreateTable(schema, s);
                else if constexpr (std::is_same_v<T, AlterTableAddColumnStmt>)
                    schema[s.tableName].push_back(s.column.name);
                else if constexpr (std::is_same_v<T, InsertStmt>)
                    ResolveInsertStatement(schema, s, sourceFilename);
            },
            stmt);
    }

} // namespace

void ResolvePositionalInserts(std::vector<ParsedMigration>& migrations)
{
    SchemaMap schema;
    for (auto& migration: migrations)
    {
        auto const filename = migration.sourceFile.filename().string();
        for (auto& stmtWithComments: migration.statements)
            ApplyStatementToSchema(schema, stmtWithComments.statement, filename);
    }
}

} // namespace Lup2DbTool
