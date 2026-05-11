// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlSchema.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace Lightweight;

namespace
{

void CreateOrdersAndItemsSchema(SqlStatement& stmt)
{
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Orders").PrimaryKeyWithAutoIncrement("OrderID").RequiredColumn(
            "Description", SqlColumnTypeDefinitions::Varchar { 100 });
    });
    stmt.MigrateDirect([](auto& migration) {
        // FK column type must match the referenced PK type. PrimaryKeyWithAutoIncrement
        // defaults to BIGINT, so the FK side must be Bigint too — MSSQL rejects mismatched types.
        migration.CreateTable("OrderItems")
            .PrimaryKeyWithAutoIncrement("ItemID")
            .ForeignKey("OrderID",
                        SqlColumnTypeDefinitions::Bigint {},
                        SqlForeignKeyReferenceDefinition { .tableName = "Orders", .columnName = "OrderID" })
            .RequiredColumn("Qty", SqlColumnTypeDefinitions::Integer {});
    });
}

} // namespace

// ================================================================================================
// AllForeignKeysFrom — list FKs declared *on* the given table
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema::AllForeignKeysFrom returns FKs declared on the child table", "[SqlSchema]")
{
    auto stmt = SqlStatement {};
    CreateOrdersAndItemsSchema(stmt);

    SqlSchema::FullyQualifiedTableName const items { .catalog = {}, .schema = {}, .table = "OrderItems" };
    auto const fks = SqlSchema::AllForeignKeysFrom(stmt, items);

    REQUIRE(fks.size() == 1);
    auto const& fk = fks.front();
    CHECK(fk.foreignKey.table.table == "OrderItems");
    REQUIRE(fk.foreignKey.columns.size() == 1);
    CHECK(fk.foreignKey.columns.front() == "OrderID");
    CHECK(fk.primaryKey.table.table == "Orders");
    REQUIRE(fk.primaryKey.columns.size() == 1);
    CHECK(fk.primaryKey.columns.front() == "OrderID");
}

// ================================================================================================
// AllForeignKeysTo — list tables that reference the given primary-key table
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema::AllForeignKeysTo returns FKs targeting the primary table", "[SqlSchema]")
{
    auto stmt = SqlStatement {};
    CreateOrdersAndItemsSchema(stmt);

    SqlSchema::FullyQualifiedTableName const orders { .catalog = {}, .schema = {}, .table = "Orders" };
    auto const fks = SqlSchema::AllForeignKeysTo(stmt, orders);

    REQUIRE(fks.size() == 1);
    auto const& fk = fks.front();
    CHECK(fk.foreignKey.table.table == "OrderItems");
    CHECK(fk.primaryKey.table.table == "Orders");
}

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema::AllForeignKeysTo returns empty for a leaf table", "[SqlSchema]")
{
    auto stmt = SqlStatement {};
    CreateOrdersAndItemsSchema(stmt);

    // OrderItems is referenced by nothing.
    SqlSchema::FullyQualifiedTableName const items { .catalog = {}, .schema = {}, .table = "OrderItems" };
    auto const fks = SqlSchema::AllForeignKeysTo(stmt, items);

    CHECK(fks.empty());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema::AllForeignKeysFrom returns empty for a table with no FKs", "[SqlSchema]")
{
    auto stmt = SqlStatement {};
    CreateOrdersAndItemsSchema(stmt);

    SqlSchema::FullyQualifiedTableName const orders { .catalog = {}, .schema = {}, .table = "Orders" };
    auto const fks = SqlSchema::AllForeignKeysFrom(stmt, orders);

    CHECK(fks.empty());
}

// ================================================================================================
// ReadAllTables (callback overload) — exercise the progress + ready callbacks
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema::ReadAllTables invokes per-table callbacks", "[SqlSchema]")
{
    auto stmt = SqlStatement {};
    CreateOrdersAndItemsSchema(stmt);

    size_t progressCalls = 0;
    size_t readyCalls = 0;
    auto const tables = SqlSchema::ReadAllTables(
        stmt,
        stmt.Connection().DatabaseName(),
        /*schema=*/"",
        /*callback=*/
        [&](std::string_view /*tableName*/, size_t /*current*/, size_t /*total*/) { ++progressCalls; },
        /*tableReadyCallback=*/
        [&](SqlSchema::Table&& table) {
            (void) std::move(table);
            ++readyCalls;
        });

    CHECK(progressCalls >= 2); // at least Orders and OrderItems
    CHECK(readyCalls == tables.size());
    CHECK(tables.size() >= 2);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema::ReadAllTables honours the table filter predicate", "[SqlSchema]")
{
    auto stmt = SqlStatement {};
    CreateOrdersAndItemsSchema(stmt);

    auto const filtered = SqlSchema::ReadAllTables(
        stmt,
        stmt.Connection().DatabaseName(),
        /*schema=*/"",
        /*callback=*/ {},
        /*tableReadyCallback=*/ {},
        /*tableFilter=*/
        [](std::string_view /*schema*/, std::string_view tableName) { return tableName == "Orders"; });

    auto const ordersDetailed =
        std::ranges::find_if(filtered, [](SqlSchema::Table const& t) { return t.name == "Orders" && !t.columns.empty(); });
    CHECK(ordersDetailed != filtered.end());

    auto const itemsDetailed = std::ranges::find_if(
        filtered, [](SqlSchema::Table const& t) { return t.name == "OrderItems" && !t.columns.empty(); });
    // OrderItems was filtered out — its detailed schema (columns, etc.) must not be populated.
    CHECK(itemsDetailed == filtered.end());
}

// ================================================================================================
// MakeCreateTablePlan — single-table and multi-table overloads
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema::MakeCreateTablePlan produces non-empty steps for a Table", "[SqlSchema]")
{
    auto stmt = SqlStatement {};
    CreateOrdersAndItemsSchema(stmt);

    auto tables = SqlSchema::ReadAllTables(stmt, stmt.Connection().DatabaseName(), /*schema=*/"");
    auto const ordersIt = std::ranges::find_if(tables, [](SqlSchema::Table const& t) { return t.name == "Orders"; });
    REQUIRE(ordersIt != tables.end());

    auto const plan = SqlSchema::MakeCreateTablePlan(*ordersIt);
    CHECK(plan.tableName == "Orders");
    CHECK_FALSE(plan.columns.empty());
}

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema::MakeCreateTablePlan (TableList) produces a plan per table", "[SqlSchema]")
{
    auto stmt = SqlStatement {};
    CreateOrdersAndItemsSchema(stmt);

    auto tables = SqlSchema::ReadAllTables(stmt, stmt.Connection().DatabaseName(), /*schema=*/"");
    REQUIRE(tables.size() >= 2);

    auto const plans = SqlSchema::MakeCreateTablePlan(tables);
    CHECK(plans.size() == tables.size());
    for (auto const& p: plans)
        CHECK_FALSE(p.columns.empty());
}
