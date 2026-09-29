// SPDX-License-Identifier: Apache-2.0

#include "SqlConnectInfo.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Lightweight
{

namespace
{
    bool IsSpace(char c) noexcept
    {
        // Cast to unsigned char before calling std::isspace — passing a signed `char`
        // whose value is not representable as unsigned char (i.e. high bit set on
        // platforms where char is signed) is undefined behaviour.
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    }

    std::string_view TrimLeft(std::string_view value) noexcept
    {
        while (!value.empty() && IsSpace(value.front()))
            value.remove_prefix(1);
        return value;
    }

    std::string_view Trim(std::string_view value) noexcept
    {
        value = TrimLeft(value);
        while (!value.empty() && IsSpace(value.back()))
            value.remove_suffix(1);
        return value;
    }

    /// Characters that make a value unsafe to splice verbatim into `KEY=VALUE;`.
    constexpr std::string_view ConnectionStringMetacharacters = ";={}";

    /// Finds the `}` that closes a brace-quoted value whose opening `{` sits at `open`, skipping
    /// every doubled `}}`.
    /// @return The index of the closing brace, or npos when the value is unterminated.
    constexpr std::size_t FindClosingBrace(std::string_view text, std::size_t open) noexcept
    {
        auto scan = text.find('}', open + 1);
        while (scan != std::string_view::npos)
        {
            if (scan + 1 >= text.size() || text[scan + 1] != '}')
                return scan;
            scan = text.find('}', scan + 2);
        }
        return std::string_view::npos;
    }

    /// Replaces every doubled `}}` in `quoted` by a single `}`.
    std::string UnescapeBraces(std::string_view quoted)
    {
        std::string result;
        result.reserve(quoted.size());
        auto doubled = quoted.find("}}");
        while (doubled != std::string_view::npos)
        {
            result.append(quoted.substr(0, doubled + 1));
            quoted.remove_prefix(doubled + 2);
            doubled = quoted.find("}}");
        }
        result.append(quoted);
        return result;
    }

    /// What @ref ScanAttributeValue reads for one attribute.
    struct ScannedValue
    {
        /// The value with brace quoting removed and every `}}` unescaped.
        std::string value;
        /// Offset of the terminating `;`, or the text size when the value runs to the end.
        std::size_t end;
    };

    /// Reads the attribute value that starts right after the `=` at `valueBegin - 1`, applying the
    /// driver managers' rules in one place: a value opening with `{` runs to the matching `}` even
    /// across `;`, an unterminated brace takes the rest of the string, and a closing brace that is
    /// followed by more text (`{q}z`) is not a quote at all, so the value is kept verbatim.
    /// @return The unquoted value and the index of its terminating `;` (or the text size).
    ScannedValue ScanAttributeValue(std::string_view text, std::size_t valueBegin)
    {
        auto const separatorFrom = [text](std::size_t from) noexcept {
            auto const separator = text.find(';', from);
            return separator == std::string_view::npos ? text.size() : separator;
        };

        auto const rest = text.substr(valueBegin);
        auto const open = valueBegin + (rest.size() - TrimLeft(rest).size());
        if (open >= text.size() || text[open] != '{')
        {
            auto const end = separatorFrom(valueBegin);
            return { .value = std::string(Trim(text.substr(valueBegin, end - valueBegin))), .end = end };
        }

        auto const close = FindClosingBrace(text, open);
        if (close == std::string_view::npos)
            return { .value = UnescapeBraces(text.substr(open + 1)), .end = text.size() };

        auto const end = separatorFrom(close + 1);
        if (!Trim(text.substr(close + 1, end - close - 1)).empty())
            return { .value = std::string(Trim(text.substr(open, end - open))), .end = end };
        return { .value = UnescapeBraces(text.substr(open + 1, close - open - 1)), .end = end };
    }

    /// One `KEY=VALUE` attribute located inside a connection string.
    struct ConnectionStringAttribute
    {
        /// The trimmed key, in its original spelling.
        std::string_view key;
        /// The value with brace quoting removed and every `}}` unescaped.
        std::string value;
        /// Offset just past the `=`.
        std::size_t valueBegin;
        /// Offset of the terminating `;`, or the text size when the attribute is the last one.
        std::size_t end;
    };

    /// Splits `text` into its `KEY=VALUE` attributes, honouring the driver managers' `{...}`
    /// quoting. Fragments without a `=` are skipped. The offsets let a caller rewrite the string
    /// attribute by attribute while preserving the spelling of everything it does not touch.
    std::vector<ConnectionStringAttribute> TokenizeConnectionString(std::string_view text)
    {
        std::vector<ConnectionStringAttribute> attributes;

        // Walk attribute by attribute rather than splitting on `;` up front: a `;` inside a
        // brace-quoted value (`PWD={p;w}`) is part of the value, not a separator.
        std::size_t pos = 0;
        while (pos < text.size())
        {
            auto const stop = text.find_first_of("=;", pos);
            if (stop == std::string_view::npos || text[stop] == ';')
            {
                // No `KEY=` before the next separator (empty fragment, `bad-pair-no-equals`, ...): skip it.
                pos = stop == std::string_view::npos ? text.size() : stop + 1;
                continue;
            }

            auto [value, end] = ScanAttributeValue(text, stop + 1);
            attributes.push_back(ConnectionStringAttribute {
                .key = Trim(text.substr(pos, stop - pos)),
                .value = std::move(value),
                .valueBegin = stop + 1,
                .end = end,
            });
            pos = end + 1;
        }

        return attributes;
    }

    std::string ToUpperCaseString(std::string_view input)
    {
        std::string result { input };
        std::ranges::transform(
            result, result.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
        return result;
    }

    /// Maps the ODBC `Encrypt=` keyword spellings onto SqlEncryptionMode. The first entry of each mode
    /// is also its canonical rendering, so the table drives both directions.
    constexpr std::array<std::pair<std::string_view, SqlEncryptionMode>, 8> EncryptionModeSpellings { {
        { "yes", SqlEncryptionMode::Enabled },
        { "true", SqlEncryptionMode::Enabled },
        { "1", SqlEncryptionMode::Enabled },
        // `mandatory` is the ODBC Driver 18 synonym of `yes`.
        { "mandatory", SqlEncryptionMode::Enabled },
        { "no", SqlEncryptionMode::Disabled },
        { "false", SqlEncryptionMode::Disabled },
        { "0", SqlEncryptionMode::Disabled },
        // `optional` is the ODBC Driver 18 synonym of `no`.
        { "optional", SqlEncryptionMode::Disabled },
    } };

    constexpr bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept
    {
        return std::ranges::equal(a, b, [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
        });
    }

} // end namespace

SqlEncryptionMode ParseEncryptionMode(std::string_view value) noexcept
{
    auto const trimmed = Trim(value);
    for (auto const& [spelling, mode]: EncryptionModeSpellings)
        if (EqualsIgnoreCase(spelling, trimmed))
            return mode;
    return SqlEncryptionMode::DriverDefault;
}

std::string_view FormatEncryptionMode(SqlEncryptionMode mode) noexcept
{
    if (mode == SqlEncryptionMode::DriverDefault)
        return {};

    for (auto const& [spelling, candidate]: EncryptionModeSpellings)
        if (candidate == mode)
            return spelling;
    return {};
}

std::string SqlConnectionString::Sanitized() const
{
    return SanitizePwd(value);
}

std::string SqlConnectionString::SanitizePwd(std::string_view input)
{
    std::regex const pwdRegex {
        R"(PWD=.*?;)",
        std::regex_constants::ECMAScript | std::regex_constants::icase,
    };
    std::stringstream outputString;
    std::regex_replace(std::ostreambuf_iterator<char> { outputString }, input.begin(), input.end(), pwdRegex, "Pwd=***;");
    return outputString.str();
}

SqlConnectionStringMap ParseConnectionString(SqlConnectionString const& connectionString)
{
    SqlConnectionStringMap result;
    for (auto& attribute: TokenizeConnectionString(connectionString.value))
        result.insert_or_assign(ToUpperCaseString(attribute.key), std::move(attribute.value));
    return result;
}

SqlConnectionString BuildConnectionString(SqlConnectionStringMap const& map)
{
    SqlConnectionString result;

    for (auto const& [key, value]: map)
    {
        std::string_view const delimiter = result.value.empty() ? "" : ";";
        std::format_to(std::back_inserter(result.value), "{}{}={}", delimiter, key, FormatConnectionStringValue(value));
    }

    return result;
}

std::string FormatConnectionStringValue(std::string_view value)
{
    auto const needsQuoting = value.find_first_of(ConnectionStringMetacharacters) != std::string_view::npos
                              || (!value.empty() && (IsSpace(value.front()) || IsSpace(value.back())));
    if (!needsQuoting)
        return std::string(value);

    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('{');
    for (auto const c: value)
    {
        result.push_back(c);
        if (c == '}')
            result.push_back('}');
    }
    result.push_back('}');
    return result;
}

bool EnsureSqliteDatabaseFileExists(SqlConnectionString const& connectionString)
{
    auto const params = ParseConnectionString(connectionString);

    auto const driverIt = params.find("DRIVER");
    if (driverIt == params.end())
        return true;

    auto const& driver = driverIt->second;
    auto const driverIsSqlite = std::ranges::search(driver,
                                                    std::string_view { "sqlite" },
                                                    [](char a, char b) {
                                                        return std::tolower(static_cast<unsigned char>(a))
                                                               == std::tolower(static_cast<unsigned char>(b));
                                                    })
                                    .begin()
                                != driver.end();
    if (!driverIsSqlite)
        return true;

    auto const databaseIt = params.find("DATABASE");
    if (databaseIt == params.end() || databaseIt->second.empty())
        return true;

    auto const& database = databaseIt->second;

    // Skip in-memory / URI forms — there is no file to create.
    if (database == ":memory:" || database.starts_with("file::memory:")
        || (database.starts_with("file:") && database.contains("mode=memory")))
        return true;

    std::filesystem::path const dbPath { database };
    std::error_code ec;

    if (auto const parent = dbPath.parent_path(); !parent.empty() && !std::filesystem::exists(parent, ec))
    {
        std::filesystem::create_directories(parent, ec);
        if (ec)
            return false;
    }

    if (!std::filesystem::exists(dbPath, ec))
    {
        std::ofstream create(dbPath, std::ios::binary);
        if (!create)
            return false;
    }

    return true;
}

SqlConnectionString SqlConnectionDataSource::ToConnectionString() const
{
    auto value = std::format("DSN={};UID={};PWD={};TIMEOUT={}",
                             FormatConnectionStringValue(datasource),
                             FormatConnectionStringValue(username),
                             FormatConnectionStringValue(password),
                             timeout.count());
    if (auto const encryptValue = FormatEncryptionMode(encryption); !encryptValue.empty())
        value += std::format(";Encrypt={}", encryptValue);
    return SqlConnectionString { .value = std::move(value) };
}

SqlConnectionDataSource SqlConnectionDataSource::FromConnectionString(SqlConnectionString const& value)
{
    auto result = SqlConnectionDataSource {};
    auto parsedConnectionStringPairs = ParseConnectionString(value);

    if (auto dsn = parsedConnectionStringPairs.extract("DSN"); !dsn.empty())
        result.datasource = std::move(dsn.mapped());

    if (auto uid = parsedConnectionStringPairs.extract("UID"); !uid.empty())
        result.username = std::move(uid.mapped());

    if (auto pwd = parsedConnectionStringPairs.extract("PWD"); !pwd.empty())
        result.password = std::move(pwd.mapped());

    if (auto timeout = parsedConnectionStringPairs.extract("TIMEOUT"); !timeout.empty())
        result.timeout = std::chrono::seconds(std::stoi(timeout.mapped()));

    if (auto encrypt = parsedConnectionStringPairs.extract("ENCRYPT"); !encrypt.empty())
        result.encryption = ParseEncryptionMode(encrypt.mapped());

    return result;
}

} // namespace Lightweight
