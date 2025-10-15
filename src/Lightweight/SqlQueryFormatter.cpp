// SPDX-License-Identifier: Apache-2.0
#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include "QueryFormatter/OracleFormatter.hpp"
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

SqlQueryFormatter const& SqlQueryFormatter::OracleSQL()
{
    static OracleSqlQueryFormatter const formatter {};
    return formatter;
}

SqlQueryFormatter const* SqlQueryFormatter::Get(SqlServerType serverType) noexcept
{
    static std::array<SqlQueryFormatter const*, 6> const formatters = {
        nullptr,
        &SqlQueryFormatter::SqlServer(),
        &SqlQueryFormatter::PostgrSQL(),
        &SqlQueryFormatter::OracleSQL(),
        &SqlQueryFormatter::Sqlite(),
        nullptr, // MySQL
    };
    return formatters[static_cast<size_t>(serverType)];
}

} // namespace Lightweight
