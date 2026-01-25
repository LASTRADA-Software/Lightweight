// SPDX-License-Identifier: Apache-2.0

#include "../DataBinder/UnicodeConverter.hpp"
#include "../SqlQueryFormatter.hpp"
#include "MigrationPlan.hpp"

#include <format>

namespace Lightweight
{

namespace
{

    /// @brief Formats a SqlVariant value as a SQL literal string.
    std::string FormatSqlLiteral(SqlQueryFormatter const& formatter, SqlVariant const& value)
    {
        using namespace std::string_literals;

        // clang-format off
        return std::visit(detail::overloaded {
            [&](SqlNullType) { return "NULL"s; },
            [&](SqlGuid guid) { return formatter.StringLiteral(std::format("{}", guid)); },
            [&](bool v) { return std::string(formatter.BooleanLiteral(v)); },
            [&](int8_t v) { return std::to_string(v); },
            [&](short v) { return std::to_string(v); },
            [&](unsigned short v) { return std::to_string(v); },
            [&](int v) { return std::to_string(v); },
            [&](unsigned int v) { return std::to_string(v); },
            [&](long long v) { return std::to_string(v); },
            [&](unsigned long long v) { return std::to_string(v); },
            [&](float v) { return std::format("{}", v); },
            [&](double v) { return std::format("{}", v); },
            [&](std::string_view v) { return formatter.StringLiteral(v); },
            [&](std::u16string_view v) {
                auto u8String = ToUtf8(v);
                return formatter.StringLiteral(std::string_view(reinterpret_cast<char const*>(u8String.data()), u8String.size()));
            },
            [&](std::u16string const& v) {
                auto u8String = ToUtf8(std::u16string_view(v));
                return formatter.StringLiteral(std::string_view(reinterpret_cast<char const*>(u8String.data()), u8String.size()));
            },
            [&](std::string const& v) { return formatter.StringLiteral(v); },
            [&](SqlText const& v) { return formatter.StringLiteral(v.value); },
            [&](SqlDate const& v) { return formatter.StringLiteral(std::format("{:04}-{:02}-{:02}", v.sqlValue.year, v.sqlValue.month, v.sqlValue.day)); },
            [&](SqlTime const& v) { return formatter.StringLiteral(std::format("{:02}:{:02}:{:02}", v.sqlValue.hour, v.sqlValue.minute, v.sqlValue.second)); },
            [&](SqlDateTime const& v) { return formatter.StringLiteral(std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", v.sqlValue.year, v.sqlValue.month, v.sqlValue.day, v.sqlValue.hour, v.sqlValue.minute, v.sqlValue.second)); }
        }, value.value);
        // clang-format on
    }

    /// @brief Formats the table name with optional schema prefix.
    std::string FormatTableName(std::string_view schemaName, std::string_view tableName)
    {
        if (schemaName.empty())
            return std::format(R"("{}")", tableName);
        return std::format(R"("{}"."{}")", schemaName, tableName);
    }

    std::vector<std::string> ToSqlInsert(SqlQueryFormatter const& formatter, SqlInsertDataPlan const& step)
    {
        std::string columns;
        std::string values;
        for (auto const& [columnName, columnValue]: step.columns)
        {
            if (!columns.empty())
            {
                columns += ", ";
                values += ", ";
            }
            columns += std::format(R"("{}")", columnName);
            values += FormatSqlLiteral(formatter, columnValue);
        }
        auto tableName = FormatTableName(step.schemaName, step.tableName);
        return { std::format("INSERT INTO {} ({}) VALUES ({})", tableName, columns, values) };
    }

    std::vector<std::string> ToSqlUpdate(SqlQueryFormatter const& formatter, SqlUpdateDataPlan const& step)
    {
        std::string setClause;
        for (auto const& [columnName, columnValue]: step.setColumns)
        {
            if (!setClause.empty())
                setClause += ", ";
            setClause += std::format(R"("{}" = {})", columnName, FormatSqlLiteral(formatter, columnValue));
        }
        auto tableName = FormatTableName(step.schemaName, step.tableName);
        std::string sql = std::format("UPDATE {} SET {}", tableName, setClause);
        if (!step.whereColumn.empty())
        {
            sql +=
                std::format(R"( WHERE "{}" {} {})", step.whereColumn, step.whereOp, FormatSqlLiteral(formatter, step.whereValue));
        }
        return { std::move(sql) };
    }

    std::vector<std::string> ToSqlDelete(SqlQueryFormatter const& formatter, SqlDeleteDataPlan const& step)
    {
        auto tableName = FormatTableName(step.schemaName, step.tableName);
        std::string sql = std::format("DELETE FROM {}", tableName);
        if (!step.whereColumn.empty())
        {
            sql +=
                std::format(R"( WHERE "{}" {} {})", step.whereColumn, step.whereOp, FormatSqlLiteral(formatter, step.whereValue));
        }
        return { std::move(sql) };
    }

    std::vector<std::string> ToSqlCreateIndex(SqlCreateIndexPlan const& step)
    {
        std::string columns;
        for (auto const& col: step.columns)
        {
            if (!columns.empty())
                columns += ", ";
            columns += std::format(R"("{}")", col);
        }

        auto tableName = FormatTableName(step.schemaName, step.tableName);
        std::string_view const uniqueStr = step.unique ? "UNIQUE " : "";
        std::string_view const ifNotExistsStr = step.ifNotExists ? "IF NOT EXISTS " : "";
        std::string indexName = step.schemaName.empty()
                                    ? std::format(R"("{}")", step.indexName)
                                    : std::format(R"("{}"."{}")", step.schemaName, step.indexName);

        return { std::format("CREATE {}INDEX {}{} ON {}({})", uniqueStr, ifNotExistsStr, indexName, tableName, columns) };
    }

} // namespace

std::vector<std::string> SqlMigrationPlan::ToSql() const
{
    std::vector<std::string> result;
    for (auto const& step: steps)
    {
        auto sql = Lightweight::ToSql(formatter, step);
        result.insert(result.end(), sql.begin(), sql.end());
    }
    return result;
}

std::vector<std::string> ToSql(SqlQueryFormatter const& formatter, SqlMigrationPlanElement const& element)
{
    using namespace std::string_literals;
    return std::visit(
        [&](auto const& step) {
            if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlCreateTablePlan>)
            {
                return formatter.CreateTable(
                    step.schemaName, step.tableName, step.columns, step.foreignKeys, step.ifNotExists);
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlAlterTablePlan>)
            {
                return formatter.AlterTable(step.schemaName, step.tableName, step.commands);
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlDropTablePlan>)
            {
                return formatter.DropTable(step.schemaName, step.tableName, step.ifExists, step.cascade);
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlCreateIndexPlan>)
            {
                return ToSqlCreateIndex(step);
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlRawSqlPlan>)
            {
                return std::vector<std::string> { std::string(step.sql) };
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlInsertDataPlan>)
            {
                return ToSqlInsert(formatter, step);
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlUpdateDataPlan>)
            {
                return ToSqlUpdate(formatter, step);
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlDeleteDataPlan>)
            {
                return ToSqlDelete(formatter, step);
            }
            else
            {
                static_assert(detail::AlwaysFalse<std::decay_t<decltype(step)>>, "non-exhaustive visitor");
            }
        },
        element);
}

std::vector<std::string> ToSql(std::vector<SqlMigrationPlan> const& plans)
{
    std::vector<std::string> result;

    for (auto const& plan: plans)
    {
        for (auto const& step: plan.steps)
        {
            auto sql = ToSql(plan.formatter, step);
            result.insert(result.end(), sql.begin(), sql.end());
        }
    }

    return result;
}

} // namespace Lightweight
