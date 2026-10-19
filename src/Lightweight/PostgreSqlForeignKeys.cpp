// SPDX-License-Identifier: Apache-2.0

#include "PostgreSqlForeignKeys.hpp"
#include "SqlOdbcWide.hpp"
#include "SqlStatement.hpp"

#include <format>
#include <string>
#include <string_view>
#include <tuple>

namespace Lightweight::detail
{

namespace
{

    /// `"name"` with `"` doubled — a PostgreSQL quoted identifier.
    std::string Quoted(std::string_view name)
    {
        auto out = std::string { "\"" };
        for (auto const c: name)
        {
            out += c;
            if (c == '"')
                out += '"';
        }
        out += '"';
        return out;
    }

    /// `'text'` with `'` doubled — a PostgreSQL string literal.
    std::string Literal(std::string_view text)
    {
        auto out = std::string { "'" };
        for (auto const c: text)
        {
            out += c;
            if (c == '\'')
                out += '\'';
        }
        out += '\'';
        return out;
    }

} // namespace

DependentObjectScript ScriptPostgreSqlForeignKeys(SqlStatement& stmt, std::vector<ColumnToRetype> const& columns)
{
    auto script = DependentObjectScript {};
    if (columns.empty())
        return script;

    // An empty schema is the connection's default one.
    auto const defaultSchema = stmt.ExecuteDirectScalar<std::string>("SELECT current_schema()").value_or(std::string {});
    auto rows = std::string {};
    for (auto const& c: columns)
        rows += std::format("{}({}, {}, {})",
                            rows.empty() ? "" : ", ",
                            Literal(c.schema.empty() ? defaultSchema : c.schema),
                            Literal(c.table),
                            Literal(c.column));

    // pg_get_constraintdef names every table with its schema while only pg_catalog is on the search
    // path; the caller's path is restored right after, so the column change runs under it.
    auto const searchPath =
        stmt.ExecuteDirectScalar<std::string>("SELECT current_setting('search_path')").value_or(std::string {});
    std::ignore = stmt.ExecuteDirectScalar<std::string>("SELECT set_config('search_path', 'pg_catalog', true)");
    {
        // Partition clones (conparentid <> 0) go and come back with the key on the partitioned table.
        auto cursor = stmt.ExecuteDirect(std::format(
            "WITH affected (relid, attnum) AS ("
            "SELECT a.attrelid, a.attnum FROM pg_attribute a JOIN pg_class r ON r.oid = a.attrelid "
            "JOIN pg_namespace n ON n.oid = r.relnamespace JOIN (VALUES {}) AS v (s, t, c) "
            "ON n.nspname = v.s AND r.relname = v.t AND a.attname = v.c) "
            "SELECT con.conname, n.nspname, r.relname, pg_get_constraintdef(con.oid) "
            "FROM pg_constraint con JOIN pg_class r ON r.oid = con.conrelid JOIN pg_namespace n ON n.oid = r.relnamespace "
            "WHERE con.contype = 'f' AND con.conparentid = 0 AND EXISTS (SELECT 1 FROM affected af WHERE "
            "(af.relid = con.conrelid AND af.attnum = ANY (con.conkey)) OR "
            "(af.relid = con.confrelid AND af.attnum = ANY (con.confkey))) "
            "ORDER BY n.nspname, r.relname, con.conname",
            rows));
        while (cursor.FetchRow())
        {
            auto const name = GetTextColumn(cursor, 1);
            auto const qualifiedTable =
                std::format("{}.{}", Quoted(GetTextColumn(cursor, 2)), Quoted(GetTextColumn(cursor, 3)));
            auto const definition = GetTextColumn(cursor, 4);
            script.drop.push_back(std::format("ALTER TABLE {} DROP CONSTRAINT {}", qualifiedTable, Quoted(name)));
            script.recreate.push_back(
                std::format("ALTER TABLE {} ADD CONSTRAINT {} {}", qualifiedTable, Quoted(name), definition));
        }
    }
    std::ignore = stmt.ExecuteDirectScalar<std::string>(
        std::format("SELECT set_config('search_path', {}, true)", Literal(searchPath)));
    return script;
}

} // namespace Lightweight::detail
