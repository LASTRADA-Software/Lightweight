// SPDX-License-Identifier: Apache-2.0

#include "../SqlQueryFormatter.hpp"
#include "MigrationPlan.hpp"

namespace Lightweight
{

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
            else if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlRawSqlPlan>)
            {
                return std::vector<std::string> { std::string(step.sql) };
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
