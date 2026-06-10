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

// ================================================================================================
// MakeColumnTypeFromMssqlSysType — pure-function mapping (no DB connection required)
//
// Verifies the batched MSSQL schema reader maps sys.types.name (+ max_length/precision/scale
// from sys.columns) to the same SqlColumnTypeDefinition variant the legacy per-table path
// produces. These tests are intentionally NOT fixture-based: they touch no database.
// ================================================================================================

namespace
{
using namespace Lightweight::SqlColumnTypeDefinitions;
using Lightweight::SqlColumnTypeDefinition;
using Lightweight::SqlSchema::detail::MakeColumnTypeFromMssqlSysType;

// Helper: map a type name with the given (max_length, precision, scale) tuple.
SqlColumnTypeDefinition Map(std::string_view name, int maxLength = 0, int precision = 0, int scale = 0)
{
    return MakeColumnTypeFromMssqlSysType(name, { .maxLength = maxLength, .precision = precision, .scale = scale });
}
} // namespace

TEST_CASE("MakeColumnTypeFromMssqlSysType maps integral and boolean types", "[schema-mapping]")
{
    CHECK(Map("int") == SqlColumnTypeDefinition { Integer {} });
    CHECK(Map("bigint") == SqlColumnTypeDefinition { Bigint {} });
    CHECK(Map("smallint") == SqlColumnTypeDefinition { Smallint {} });
    CHECK(Map("tinyint") == SqlColumnTypeDefinition { Tinyint {} });
    CHECK(Map("bit") == SqlColumnTypeDefinition { Bool {} });
}

TEST_CASE("MakeColumnTypeFromMssqlSysType maps numeric types", "[schema-mapping]")
{
    CHECK(Map("decimal", /*maxLength=*/9, /*precision=*/18, /*scale=*/4)
          == SqlColumnTypeDefinition { Decimal { .precision = 18, .scale = 4 } });
    CHECK(Map("numeric", 5, 10, 2) == SqlColumnTypeDefinition { Decimal { .precision = 10, .scale = 2 } });
    // money: sys.columns reports precision 19, scale 4.
    CHECK(Map("money", 8, 19, 4) == SqlColumnTypeDefinition { Decimal { .precision = 19, .scale = 4 } });
    // smallmoney: precision 10, scale 4.
    CHECK(Map("smallmoney", 4, 10, 4) == SqlColumnTypeDefinition { Decimal { .precision = 10, .scale = 4 } });
    // float/real both collapse to Real{53} (matches the legacy MSSQL float fixup).
    CHECK(Map("float", 8, 53, 0) == SqlColumnTypeDefinition { Real { .precision = 53 } });
    CHECK(Map("real", 4, 24, 0) == SqlColumnTypeDefinition { Real { .precision = 53 } });
}

TEST_CASE("MakeColumnTypeFromMssqlSysType maps non-Unicode character types", "[schema-mapping]")
{
    CHECK(Map("char", /*maxLength=*/10) == SqlColumnTypeDefinition { Char { .size = 10 } });
    CHECK(Map("varchar", 255) == SqlColumnTypeDefinition { Varchar { .size = 255 } });
    // varchar(max) — max_length == -1 -> LOB sentinel (matches the legacy SQLColumns COLUMN_SIZE).
    CHECK(Map("varchar", -1) == SqlColumnTypeDefinition { Varchar { .size = 2147483647 } });
    // text (LOB) -> Varchar with the same non-Unicode LOB sentinel.
    CHECK(Map("text", 16) == SqlColumnTypeDefinition { Varchar { .size = 2147483647 } });
}

TEST_CASE("MakeColumnTypeFromMssqlSysType maps Unicode character types (size = bytes/2)", "[schema-mapping]")
{
    // nchar(10): max_length = 20 bytes -> size 10 characters.
    CHECK(Map("nchar", /*maxLength=*/20) == SqlColumnTypeDefinition { NChar { .size = 10 } });
    // nvarchar(255): max_length = 510 bytes -> size 255.
    CHECK(Map("nvarchar", 510) == SqlColumnTypeDefinition { NVarchar { .size = 255 } });
    // nvarchar(max) -> Unicode LOB sentinel (matches the legacy SQLColumns COLUMN_SIZE).
    CHECK(Map("nvarchar", -1) == SqlColumnTypeDefinition { NVarchar { .size = 1073741823 } });
    // ntext (LOB) -> NVarchar with the same Unicode LOB sentinel.
    CHECK(Map("ntext", 16) == SqlColumnTypeDefinition { NVarchar { .size = 1073741823 } });
}

TEST_CASE("MakeColumnTypeFromMssqlSysType maps binary types", "[schema-mapping]")
{
    CHECK(Map("binary", /*maxLength=*/16) == SqlColumnTypeDefinition { Binary { .size = 16 } });
    CHECK(Map("varbinary", 255) == SqlColumnTypeDefinition { VarBinary { .size = 255 } });
    // varbinary(max) -> LOB sentinel (matches the legacy SQLColumns COLUMN_SIZE).
    CHECK(Map("varbinary", -1) == SqlColumnTypeDefinition { VarBinary { .size = 2147483647 } });
    // image (LOB) -> VarBinary with the same non-Unicode LOB sentinel.
    CHECK(Map("image", 16) == SqlColumnTypeDefinition { VarBinary { .size = 2147483647 } });
}

TEST_CASE("MakeColumnTypeFromMssqlSysType maps identifier and temporal types", "[schema-mapping]")
{
    CHECK(Map("uniqueidentifier", 16) == SqlColumnTypeDefinition { Guid {} });
    CHECK(Map("date", 3) == SqlColumnTypeDefinition { Date {} });
    CHECK(Map("time", 5) == SqlColumnTypeDefinition { Time {} });
    CHECK(Map("datetime", 8) == SqlColumnTypeDefinition { DateTime {} });
    CHECK(Map("datetime2", 8) == SqlColumnTypeDefinition { DateTime {} });
    CHECK(Map("smalldatetime", 4) == SqlColumnTypeDefinition { DateTime {} });
    CHECK(Map("datetimeoffset", 10) == SqlColumnTypeDefinition { DateTime {} });
    // timestamp/rowversion are 8-byte binary stamps.
    CHECK(Map("timestamp", 8) == SqlColumnTypeDefinition { VarBinary { .size = 8 } });
    CHECK(Map("rowversion", 8) == SqlColumnTypeDefinition { VarBinary { .size = 8 } });
}
