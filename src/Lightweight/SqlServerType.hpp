// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

#include <cstdint>
#include <format>

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
