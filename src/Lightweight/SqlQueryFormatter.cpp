// SPDX-License-Identifier: Apache-2.0

#include "QueryFormatter/OracleFormatter.hpp"
#include "QueryFormatter/PostgreSqlFormatter.hpp"
#include "QueryFormatter/SQLiteFormatter.hpp"
#include "QueryFormatter/SqlServerFormatter.hpp"
#include "SqlQueryFormatter.hpp"

#include <reflection-cpp/reflection.hpp>

using namespace std::string_view_literals;

SqlQueryFormatter const& SqlQueryFormatter::Sqlite()
{
    static const SQLiteQueryFormatter formatter {};
    return formatter;
}

SqlQueryFormatter const& SqlQueryFormatter::SqlServer()
{
    static const SqlServerQueryFormatter formatter {};
    return formatter;
}

SqlQueryFormatter const& SqlQueryFormatter::PostgrSQL()
{
    static const PostgreSqlFormatter formatter {};
    return formatter;
}

SqlQueryFormatter const& SqlQueryFormatter::OracleSQL()
{
    static const OracleSqlQueryFormatter formatter {};
    return formatter;
}

SqlQueryFormatter const* SqlQueryFormatter::Get(SqlServerType serverType) noexcept
{
    static const std::array<SqlQueryFormatter const*, 6> formatters = {
        nullptr,
        &SqlQueryFormatter::SqlServer(),
        &SqlQueryFormatter::PostgrSQL(),
        &SqlQueryFormatter::OracleSQL(),
        &SqlQueryFormatter::Sqlite(),
        nullptr, // MySQL
    };
    return formatters[static_cast<size_t>(serverType)];
}
