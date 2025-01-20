// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <string_view>

// Represents the type of SQL server, used to determine the correct SQL syntax, if needed.
enum class SqlServerType : uint8_t
{
    UNKNOWN,
    MICROSOFT_SQL,
    POSTGRESQL,
    ORACLE,
    SQLITE,
    MYSQL,
};

struct SqlTraits
{
    std::string_view PrimaryKeyAutoIncrement; // Maybe rename this to `PrimaryKeyIdentityColumnType`?
    std::string_view CurrentTimestampExpr;
    size_t MaxStatementLength {};
};

namespace detail
{

inline SqlTraits const MicrosoftSqlTraits {
    .PrimaryKeyAutoIncrement = "INT IDENTITY(1,1) PRIMARY KEY",
    .CurrentTimestampExpr = "GETDATE()",
};

inline SqlTraits const PostgresSqlTraits {
    .PrimaryKeyAutoIncrement = "SERIAL PRIMARY KEY",
    .CurrentTimestampExpr = "CURRENT_TIMESTAMP",
};

inline SqlTraits const OracleSqlTraits {
    .PrimaryKeyAutoIncrement = "NUMBER GENERATED BY DEFAULT ON NULL AS IDENTITY PRIMARY KEY",
    .CurrentTimestampExpr = "SYSTIMESTAMP",
};

inline SqlTraits const SQLiteTraits {
    .PrimaryKeyAutoIncrement = "INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT",
    .CurrentTimestampExpr = "CURRENT_TIMESTAMP",
};

inline SqlTraits const MySQLTraits {
    .PrimaryKeyAutoIncrement = "INT AUTO_INCREMENT PRIMARY KEY",
    .CurrentTimestampExpr = "NOW()",
};

inline SqlTraits const UnknownSqlTraits {
    .PrimaryKeyAutoIncrement = "",
    .CurrentTimestampExpr = "",
};

} // namespace detail

inline SqlTraits const& GetSqlTraits(SqlServerType serverType) noexcept
{
    auto static const sqlTraits = std::array {
        &detail::UnknownSqlTraits, &detail::MicrosoftSqlTraits, &detail::PostgresSqlTraits,
        &detail::OracleSqlTraits,  &detail::SQLiteTraits,
    };

    return *sqlTraits[static_cast<size_t>(serverType)];
}

template <>
struct LIGHTWEIGHT_API std::formatter<SqlServerType>: std::formatter<std::string_view>
{
    auto format(SqlServerType type, format_context& ctx) const -> format_context::iterator
    {
        string_view name;
        switch (type)
        {
            case SqlServerType::MICROSOFT_SQL:
                name = "Microsoft SQL Server";
                break;
            case SqlServerType::POSTGRESQL:
                name = "PostgreSQL";
                break;
            case SqlServerType::ORACLE:
                name = "Oracle";
                break;
            case SqlServerType::SQLITE:
                name = "SQLite";
                break;
            case SqlServerType::MYSQL:
                name = "MySQL";
                break;
            case SqlServerType::UNKNOWN:
                name = "Unknown";
                break;
        }
        return std::formatter<string_view>::format(name, ctx);
    }
};
