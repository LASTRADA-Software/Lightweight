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
        auto subSteps = Lightweight::ToSql(formatter, step);
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

} // namespace Lightweight
