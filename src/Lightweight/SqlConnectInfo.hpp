// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

#include <chrono>
#include <format>
#include <map>
#include <string>
#include <variant>

namespace Lightweight
{

/// Represents an ODBC connection string.
struct SqlConnectionString
{
    std::string value;

    auto operator<=>(SqlConnectionString const&) const noexcept = default;

    [[nodiscard]] LIGHTWEIGHT_API std::string Sanitized() const;

    [[nodiscard]] LIGHTWEIGHT_API static std::string SanitizePwd(std::string_view input);
};

using SqlConnectionStringMap = std::map<std::string, std::string>;

/// Parses an ODBC connection string into a map.
LIGHTWEIGHT_API SqlConnectionStringMap ParseConnectionString(SqlConnectionString const& connectionString);

/// Builds an ODBC connection string from a map.
LIGHTWEIGHT_API SqlConnectionString BuildConnectionString(SqlConnectionStringMap const& map);

/// Represents a connection data source as a DSN, username, password, and timeout.
struct [[nodiscard]] SqlConnectionDataSource
{
    std::string datasource;
    std::string username;
    std::string password;
    std::chrono::seconds timeout { 5 };

    LIGHTWEIGHT_API static SqlConnectionDataSource FromConnectionString(SqlConnectionString const& value);

    [[nodiscard]] LIGHTWEIGHT_API SqlConnectionString ToConnectionString() const
    {
        return SqlConnectionString {
            .value = std::format("DSN={};UID={};PWD={};TIMEOUT={}", datasource, username, password, timeout.count())
        };
    }

    auto operator<=>(SqlConnectionDataSource const&) const noexcept = default;
};

using SqlConnectInfo = std::variant<SqlConnectionDataSource, SqlConnectionString>;

} // namespace Lightweight

template <>
struct std::formatter<Lightweight::SqlConnectInfo>: std::formatter<std::string>
{
    auto format(Lightweight::SqlConnectInfo const& info, format_context& ctx) const -> format_context::iterator
    {
        if (auto const* dsn = std::get_if<Lightweight::SqlConnectionDataSource>(&info))
        {
            return formatter<string>::format(
                std::format(
                    "DSN={};UID={};PWD={};TIMEOUT={}", dsn->datasource, dsn->username, dsn->password, dsn->timeout.count()),
                ctx);
        }
        else if (auto const* connectionString = std::get_if<Lightweight::SqlConnectionString>(&info))
        {
            return formatter<string>::format(connectionString->value, ctx);
        }
        else
        {
            return formatter<string>::format("Invalid connection info", ctx);
        }
    }
};
