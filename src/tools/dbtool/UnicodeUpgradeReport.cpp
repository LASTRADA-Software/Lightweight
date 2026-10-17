// SPDX-License-Identifier: Apache-2.0

#include "UnicodeUpgradeReport.hpp"

#include <algorithm>
#include <format>
#include <print>
#include <string>

namespace Lightweight::Tools
{

void PrintUnicodeUpgradePlan(std::ostream& out, SqlMigration::MigrationManager::UnicodeUpgradeResult const& plan)
{
    std::println(out, "Columns to upgrade ({}):", plan.columns.size());
    // Like the backup listing, names the schema only when the columns span several.
    auto const spansSchemas = std::ranges::any_of(
        plan.columns, [&](auto const& column) { return column.table.schema != plan.columns.front().table.schema; });
    auto const tableName = [spansSchemas](SqlSchema::FullyQualifiedTableName const& table) {
        return spansSchemas ? std::format("{}.{}", table.schema, table.table) : table.table;
    };
    for (auto const& column: plan.columns)
    {
        if (column.joinedByForeignKeyTo.empty())
            std::println(out, "  {}.{}", tableName(column.table), column.column);
        else
            std::println(out,
                         "  {}.{} (not declared; joined by a foreign key to {})",
                         tableName(column.table),
                         column.column,
                         column.joinedByForeignKeyTo);
    }

    // Each unit is committed on its own; only units of several tables are worth listing.
    std::println(out, "");
    std::println(out, "Transactions: {}, one per unit; the tables a foreign key joins share a unit.", plan.units.size());
    auto unitNumber = std::size_t { 0 };
    for (auto const& unit: plan.units)
    {
        ++unitNumber;
        if (unit.size() < 2)
            continue;
        auto tables = std::string {};
        for (auto const& table: unit)
            tables += std::format("{}{}", tables.empty() ? "" : ", ", tableName(table));
        std::println(out, "  unit {}: {}", unitNumber, tables);
    }

    if (!plan.rebuiltObjects.empty())
    {
        // SQL Server: every key, constraint, index and statistic on the columns.
        std::println(out, "");
        std::println(out, "Objects to drop and recreate under their original names ({}):", plan.rebuiltObjects.size());
        for (auto const& object: plan.rebuiltObjects)
            std::println(out, "  {}", object);
    }
    else if (!plan.rebuiltForeignKeys.empty())
    {
        std::println(out, "");
        std::println(out, "Foreign keys to drop and re-add ({}):", plan.rebuiltForeignKeys.size());
        for (auto const& foreignKey: plan.rebuiltForeignKeys)
        {
            std::print(out, "  ");
            for (auto const& column: foreignKey.columns)
                std::print(out, "{} ", column);
            std::println(out, "→ {}", foreignKey.referencedTableName);
        }
    }
}

} // namespace Lightweight::Tools
