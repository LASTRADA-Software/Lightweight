// SPDX-License-Identifier: Apache-2.0

#include "NewMigration.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <fstream>
#include <system_error>
#include <vector>

namespace Lightweight::Tools
{

namespace
{

    /// The digits of a `YYYYMMDDHHMMSS` timestamp.
    constexpr auto TimestampDigits = std::size_t { 14 };

    /// File names stay short enough for every file system, however long the title is.
    constexpr auto MaxSlugLength = std::size_t { 64 };

    /// Whether @p c is an ASCII digit. Not `std::isdigit`, which depends on the locale.
    [[nodiscard]] constexpr bool IsAsciiDigit(char c) noexcept
    {
        return c >= '0' && c <= '9';
    }

    /// Whether @p c is an ASCII letter or digit.
    [[nodiscard]] constexpr bool IsAsciiAlphanumeric(char c) noexcept
    {
        return IsAsciiDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    /// Lowers an ASCII letter and leaves any other character alone.
    [[nodiscard]] constexpr char ToAsciiLower(char c) noexcept
    {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }

    /// Whether @p c is a C0 control character or DEL. Bytes of UTF-8 sequences are not.
    [[nodiscard]] constexpr bool IsControlCharacter(char c) noexcept
    {
        return static_cast<unsigned char>(c) < 0x20 || c == 0x7F;
    }

    /// Formats a time point as the integer literal the migration macros take.
    /// Assembled from calendar parts, because libc++ builds of this library lack some `chrono` formatters.
    [[nodiscard]] std::string FormatMigrationTimestamp(std::chrono::sys_seconds timestamp)
    {
        auto const day = std::chrono::floor<std::chrono::days>(timestamp);
        auto const date = std::chrono::year_month_day { day };
        auto const time = std::chrono::hh_mm_ss { timestamp - day };
        return std::format("{:04}{:02}{:02}{:02}{:02}{:02}",
                           static_cast<int>(date.year()),
                           static_cast<unsigned>(date.month()),
                           static_cast<unsigned>(date.day()),
                           time.hours().count(),
                           time.minutes().count(),
                           time.seconds().count());
    }

    /// Reduces @p title to lowercase ASCII words joined by underscores, safe in a file name on every platform.
    [[nodiscard]] std::string MakeSlug(std::string_view title)
    {
        auto slug = std::string {};
        for (char const c: title)
        {
            if (IsAsciiAlphanumeric(c))
                slug.push_back(ToAsciiLower(c));
            else if (!slug.empty() && slug.back() != '_')
                slug.push_back('_');
            if (slug.size() == MaxSlugLength)
                break;
        }
        if (!slug.empty() && slug.back() == '_')
            slug.pop_back();
        return slug;
    }

    /// Escapes @p title for use inside a C++ string literal.
    [[nodiscard]] std::string EscapeForStringLiteral(std::string_view title)
    {
        auto escaped = std::string {};
        escaped.reserve(title.size());
        for (char const c: title)
        {
            if (c == '"' || c == '\\')
                escaped.push_back('\\');
            escaped.push_back(c);
        }
        return escaped;
    }

    /// Parses the base-10 number in @p digits, which holds nothing but digits.
    [[nodiscard]] int ParseNumber(std::string_view digits) noexcept
    {
        auto value = 0;
        std::from_chars(digits.data(), digits.data() + digits.size(), value);
        return value;
    }

} // namespace

std::optional<std::chrono::sys_seconds> ParseMigrationFileTimestamp(std::string_view fileName)
{
    if (fileName.size() < TimestampDigits)
        return std::nullopt;
    auto const digits = fileName.substr(0, TimestampDigits);
    if (!std::ranges::all_of(digits, IsAsciiDigit))
        return std::nullopt;
    if (fileName.size() > TimestampDigits && IsAsciiDigit(fileName[TimestampDigits]))
        return std::nullopt;

    auto const date =
        std::chrono::year_month_day { std::chrono::year { ParseNumber(digits.substr(0, 4)) },
                                      std::chrono::month { static_cast<unsigned>(ParseNumber(digits.substr(4, 2))) },
                                      std::chrono::day { static_cast<unsigned>(ParseNumber(digits.substr(6, 2))) } };
    auto const hours = ParseNumber(digits.substr(8, 2));
    auto const minutes = ParseNumber(digits.substr(10, 2));
    auto const seconds = ParseNumber(digits.substr(12, 2));
    if (!date.ok() || hours > 23 || minutes > 59 || seconds > 59)
        return std::nullopt;

    return std::chrono::sys_days { date } + std::chrono::hours { hours } + std::chrono::minutes { minutes }
           + std::chrono::seconds { seconds };
}

std::chrono::sys_seconds NextMigrationTimestamp(std::chrono::sys_seconds now,
                                                std::span<std::chrono::sys_seconds const> existing) noexcept
{
    if (existing.empty())
        return now;
    return (std::max) (now, std::ranges::max(existing) + std::chrono::seconds { 1 });
}

std::expected<NewMigrationFile, std::string> MakeNewMigrationFile(std::string_view title, std::chrono::sys_seconds timestamp)
{
    if (std::ranges::all_of(title, [](char c) { return c == ' ' || c == '\t'; }))
        return std::unexpected { std::string { "the migration title must not be blank" } };
    if (std::ranges::any_of(title, IsControlCharacter))
        return std::unexpected { std::string { "the migration title must not contain control characters" } };

    auto const literal = FormatMigrationTimestamp(timestamp);
    auto const slug = MakeSlug(title);
    return NewMigrationFile {
        .fileName = slug.empty() ? std::format("{}.cpp", literal) : std::format("{}_{}.cpp", literal, slug),
        .source = std::format(R"cpp(#include <Lightweight/SqlMigration.hpp>

using namespace Lightweight::SqlColumnTypeDefinitions;

LIGHTWEIGHT_SQL_MIGRATION({}, "{}")
{{
    // Describe the schema change on `plan`, for example:
    //     plan.CreateTable("users").PrimaryKeyWithAutoIncrement("id").RequiredColumn("email", Varchar(255));
    // Remove the line below once `plan` is used. See docs/sql-migrations.md.
    static_cast<void>(plan);
}}
)cpp",
                              literal,
                              EscapeForStringLiteral(title)),
    };
}

std::expected<std::filesystem::path, std::string> WriteNewMigrationFile(std::string_view title,
                                                                        std::filesystem::path const& directory,
                                                                        std::chrono::sys_seconds now)
{
    auto errorCode = std::error_code {};
    if (!std::filesystem::is_directory(directory, errorCode))
        return std::unexpected { std::format("{} is not a directory", directory.string()) };

    auto existing = std::vector<std::chrono::sys_seconds> {};
    for (auto const& entry: std::filesystem::directory_iterator { directory, errorCode })
        if (auto const timestamp = ParseMigrationFileTimestamp(entry.path().filename().string()); timestamp.has_value())
            existing.push_back(*timestamp);
    if (errorCode)
        return std::unexpected { std::format("Cannot read {}: {}", directory.string(), errorCode.message()) };

    auto file = MakeNewMigrationFile(title, NextMigrationTimestamp(now, existing));
    if (!file)
        return std::unexpected { std::move(file).error() };

    auto path = directory / file->fileName;
    auto stream = std::ofstream { path, std::ios::binary };
    stream << file->source;
    stream.close();
    if (!stream)
        return std::unexpected { std::format("Cannot write {}", path.string()) };
    return path;
}

} // namespace Lightweight::Tools
