// SPDX-License-Identifier: Apache-2.0

#include "../SqlQueryFormatter.hpp"
#include "MigrationPlan.hpp"

std::string SqlMigrationPlan::ToSql() const
{
    std::string result;
    for (auto const& step: steps)
        result += ::ToSql(formatter, step);
    return result;
}

std::string ToSql(SqlQueryFormatter const& formatter, SqlMigrationPlanElement const& element)
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
