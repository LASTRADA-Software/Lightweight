// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the plan `dbtool unicode-upgrade-tables` prints.

#include "../../tools/dbtool/UnicodeUpgradeReport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

using Lightweight::SqlMigration::MigrationManager;
using Lightweight::SqlSchema::FullyQualifiedTableName;
using Lightweight::Tools::PrintUnicodeUpgradePlan;

namespace
{

/// A key column of @p schema.@p table widened from CHAR(10) to NCHAR(10).
MigrationManager::ColumnUpgradeEntry KeyColumn(std::string schema, std::string table, std::string joinedTo = {})
{
    using namespace Lightweight::SqlColumnTypeDefinitions;
    return MigrationManager::ColumnUpgradeEntry {
        .table = FullyQualifiedTableName { .catalog = {}, .schema = std::move(schema), .table = std::move(table) },
        .column = "CODE",
        .liveType = Char { 10 },
        .intendedType = NChar { 10 },
        .nullable = false,
        .joinedByForeignKeyTo = std::move(joinedTo),
    };
}

/// The plan as dbtool prints it.
std::string Printed(MigrationManager::UnicodeUpgradeResult const& plan)
{
    auto out = std::ostringstream {};
    PrintUnicodeUpgradePlan(out, plan);
    return out.str();
}

} // namespace

TEST_CASE("PrintUnicodeUpgradePlan: names the schemas when the columns span several", "[dbtool][UnicodeUpgrade]")
{
    auto plan = MigrationManager::UnicodeUpgradeResult {};
    plan.wasDryRun = true;
    plan.columns = { KeyColumn("dbo", "customers"), KeyColumn("sales", "orders", "dbo.customers.CODE") };
    plan.units = { { plan.columns[0].table, plan.columns[1].table } };

    auto const text = Printed(plan);
    CHECK(text.contains("Columns to upgrade (2):\n"));
    CHECK(text.contains("\n  dbo.customers.CODE\n"));
    CHECK(text.contains("\n  sales.orders.CODE (not declared; joined by a foreign key to dbo.customers.CODE)\n"));
    CHECK(text.contains("Transactions: 1, one per unit"));
    CHECK(text.contains("\n  unit 1: dbo.customers, sales.orders\n"));
}

TEST_CASE("PrintUnicodeUpgradePlan: leaves the schema out when all columns share one", "[dbtool][UnicodeUpgrade]")
{
    auto plan = MigrationManager::UnicodeUpgradeResult {};
    plan.wasDryRun = true;
    plan.columns = { KeyColumn("dbo", "customers"), KeyColumn("dbo", "orders") };
    plan.units = { { plan.columns[0].table }, { plan.columns[1].table } };

    auto const text = Printed(plan);
    CHECK(text.contains("\n  customers.CODE\n"));
    CHECK(text.contains("\n  orders.CODE\n"));
    CHECK_FALSE(text.contains("dbo."));
    CHECK(text.contains("Transactions: 2, one per unit"));
    // Units of a single table are not listed.
    CHECK_FALSE(text.contains("unit 1:"));
}
