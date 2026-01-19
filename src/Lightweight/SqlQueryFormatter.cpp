// SPDX-License-Identifier: Apache-2.0

#include "QueryFormatter/PostgreSqlFormatter.hpp"
#include "QueryFormatter/SQLiteFormatter.hpp"
#include "QueryFormatter/SqlServerFormatter.hpp"
#include "SqlQueryFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

using namespace std::string_view_literals;

namespace Lightweight
{

SqlQueryFormatter const& SqlQueryFormatter::Sqlite()
{
    static SQLiteQueryFormatter const formatter {};
    return formatter;
}

SqlQueryFormatter const& SqlQueryFormatter::SqlServer()
{
    static SqlServerQueryFormatter const formatter {};
    return formatter;
}

SqlQueryFormatter const& SqlQueryFormatter::PostgrSQL()
{
    static PostgreSqlFormatter const formatter {};
    return formatter;
}

SqlQueryFormatter const* SqlQueryFormatter::Get(SqlServerType serverType) noexcept
{
    static std::array<SqlQueryFormatter const*, 5> const formatters = {
        nullptr,
        &SqlQueryFormatter::SqlServer(),
        &SqlQueryFormatter::PostgrSQL(),
        &SqlQueryFormatter::Sqlite(),
        nullptr, // MySQL
    };
    return formatters[static_cast<size_t>(serverType)];
}

} // namespace Lightweight
