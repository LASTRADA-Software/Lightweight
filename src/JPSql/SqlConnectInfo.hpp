#pragma once

#include <chrono>
#include <format>
#include <string>
#include <variant>

struct SqlConnectionString
{
    std::string connectionString;

    auto operator<=>(SqlConnectionString const&) const noexcept = default;
};

struct SqlConnectionDataSource
{
    std::string datasource;
    std::string username;
    std::string password;
    std::chrono::seconds timeout { 5 };

    auto operator<=>(SqlConnectionDataSource const&) const noexcept = default;
};

using SqlConnectInfo = std::variant<SqlConnectionDataSource, SqlConnectionString>;

template <>
struct std::formatter<SqlConnectInfo>: std::formatter<std::string>
{
    auto format(SqlConnectInfo const& info, format_context& ctx) const -> format_context::iterator
    {
        if (auto const* dsn = std::get_if<SqlConnectionDataSource>(&info))
        {
            return formatter<string>::format(
                std::format(
                    "DSN={};UID={};PWD={};TIMEOUT={}", dsn->datasource, dsn->username, dsn->password, dsn->timeout.count()),
                ctx);
        }
        else if (auto const* connectionString = std::get_if<SqlConnectionString>(&info))
        {
            return formatter<string>::format(connectionString->connectionString, ctx);
        }
        else
        {
            return formatter<string>::format("Invalid connection info", ctx);
        }
    }
};
