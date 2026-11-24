// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

namespace Lightweight
{

enum class SqlServerType : uint8_t
{
    UNKNOWN,
    MICROSOFT_SQL,
    POSTGRESQL,
    SQLITE,
    MYSQL,
};

/// Maps the DBMS name reported by ODBC (SQL_DBMS_NAME) to a SqlServerType.
///
/// A name matches if it contains the well-known DBMS name as a substring.
///
/// @param serverName The DBMS name as reported by the driver.
/// @return The matching server type, or SqlServerType::UNKNOWN if none matches.
[[nodiscard]] constexpr SqlServerType ServerTypeFromServerName(std::string_view serverName) noexcept
{
    using namespace std::string_view_literals;
    constexpr auto mappings = std::array {
        std::pair { "Microsoft SQL Server"sv, SqlServerType::MICROSOFT_SQL },
        std::pair { "PostgreSQL"sv, SqlServerType::POSTGRESQL },
        std::pair { "SQLite"sv, SqlServerType::SQLITE },
        std::pair { "MySQL"sv, SqlServerType::MYSQL },
    };
    for (auto const& [name, type]: mappings)
        if (serverName.contains(name))
            return type;
    return SqlServerType::UNKNOWN;
}

/// Tells the backend an ODBC driver serves from the driver's name or library path, as a connection string's
/// @c DRIVER attribute or a data source's @c Driver entry gives it, before anything is connected.
///
/// The match is a case-insensitive substring, so a registered name (@c SQLite3, @c {PostgreSQL Unicode}), a
/// lower-case spelling, and a library path (@c /usr/lib/.../libsqlite3odbc.so, @c sqlite3odbc.dll) all match.
///
/// @param driverName The driver's name or path; braces are allowed.
/// @return The backend, or SqlServerType::UNKNOWN when no known name occurs in @p driverName.
[[nodiscard]] constexpr SqlServerType ServerTypeFromDriverName(std::string_view driverName) noexcept
{
    using namespace std::string_view_literals;
    // Checked in order: "sqlite" first, since a SQLite path or name may also contain "sql".
    constexpr auto mappings = std::array {
        std::pair { "sqlite"sv, SqlServerType::SQLITE },
        std::pair { "postgres"sv, SqlServerType::POSTGRESQL },
        std::pair { "psqlodbc"sv, SqlServerType::POSTGRESQL },
        std::pair { "sql server"sv, SqlServerType::MICROSOFT_SQL },
        std::pair { "msodbcsql"sv, SqlServerType::MICROSOFT_SQL },
        std::pair { "mysql"sv, SqlServerType::MYSQL },
    };
    constexpr auto lower = [](char c) noexcept {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (auto const& [needle, type]: mappings)
        if (!std::ranges::search(driverName, needle, {}, lower).empty())
            return type;
    return SqlServerType::UNKNOWN;
}

namespace detail
{
    /// How the narrow (@c char) string binders exchange text with a backend's driver.
    enum class NarrowTextBinding : uint8_t
    {
        /// Bound as @c SQL_C_CHAR; the driver keeps the bytes as they are.
        Narrow,
        /// Bound as @c SQL_C_WCHAR, converting the narrow text as UTF-8.
        WideFromUtf8,
        /// Bound as @c SQL_C_WCHAR, converting the narrow text with the process ANSI code page.
        WideFromProcessCodePage,
    };

    /// Returns how narrow strings are bound on @p serverType when the connection options do not decide it.
    ///
    /// psqlODBC on Windows transcodes @c SQL_C_CHAR through the system code page, so PostgreSQL binds
    /// narrow strings wide and treats them as UTF-8. A connection may refine this; see
    /// @c SqlConnection::NarrowTextBindingMode().
    ///
    /// @param serverType The backend server type.
    /// @return The binding the narrow string binders use on that backend.
    [[nodiscard]] constexpr NarrowTextBinding NarrowTextBindingOf(SqlServerType serverType) noexcept
    {
        switch (serverType)
        {
            case SqlServerType::POSTGRESQL:
                return NarrowTextBinding::WideFromUtf8;
            case SqlServerType::MICROSOFT_SQL:
            case SqlServerType::SQLITE:
            case SqlServerType::MYSQL:
            case SqlServerType::UNKNOWN:
                return NarrowTextBinding::Narrow;
        }
        return NarrowTextBinding::Narrow;
    }
} // namespace detail

} // namespace Lightweight

template <>
struct std::formatter<Lightweight::SqlServerType>: std::formatter<std::string_view>
{
    using SqlServerType = Lightweight::SqlServerType;
    auto format(SqlServerType type, format_context& ctx) const -> format_context::iterator
    {
        using namespace std::string_view_literals;
        string_view name;
        switch (type)
        {
            case SqlServerType::MICROSOFT_SQL:
                name = "Microsoft SQL Server"sv;
                break;
            case SqlServerType::POSTGRESQL:
                name = "PostgreSQL"sv;
                break;
            case SqlServerType::SQLITE:
                name = "SQLite"sv;
                break;
            case SqlServerType::MYSQL:
                name = "MySQL"sv;
                break;
            case SqlServerType::UNKNOWN:
                name = "Unknown"sv;
                break;
        }
        return std::formatter<string_view>::format(name, ctx);
    }
};
