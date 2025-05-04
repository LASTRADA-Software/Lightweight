// SPDX-License-Identifier: Apache-2.0

#include "../SqlQueryFormatter.hpp"
#include "MigrationPlan.hpp"

std::vector<std::string> SqlMigrationPlan::ToSql() const
{
    std::vector<std::string> result;
    for (auto const& step: steps)
    {
        auto subSteps = ::ToSql(formatter, step);
        result.insert(result.end(), subSteps.begin(), subSteps.end());
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
                return formatter.CreateTable(step.tableName, step.columns);
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlAlterTablePlan>)
            {
                return formatter.AlterTable(step.tableName, step.commands);
            }
            else if constexpr (std::is_same_v<std::decay_t<decltype(step)>, SqlDropTablePlan>)
            {
                return formatter.DropTable(step.tableName);
            }
            else
            {
                static_assert(false, "non-exhaustive visitor");
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
            auto subSteps = ToSql(plan.formatter, step);
            result.insert(result.end(), subSteps.begin(), subSteps.end());
        }
    }

    return result;
}

