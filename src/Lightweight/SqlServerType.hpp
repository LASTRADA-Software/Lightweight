// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

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
