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
// Auto-increment primary keys must keep the integer width the record declared.
//
// Regression guard for issue #521: the PostgreSQL formatter used to emit a hard-coded SERIAL
// (32-bit) for every auto-increment column, so a Field<int64_t, PrimaryKey::ServerSideAutoIncrement>
// silently became an `integer` column. The SQL-generation tests could not catch that on their own
// because they asserted the generated text; this one reads the type back out of the live catalog.
// ================================================================================================

namespace
{

/// Reads the live type of a single column back from the database catalog.
SqlColumnTypeDefinition LiveColumnType(SqlStatement& stmt, std::string_view tableName, std::string_view columnName)
{
    auto const tables = SqlSchema::ReadAllTables(stmt, stmt.Connection().DatabaseName(), /*schema=*/"");
    auto const table = std::ranges::find_if(tables, [&](SqlSchema::Table const& t) { return t.name == tableName; });
    REQUIRE(table != tables.end());

    auto const column =
        std::ranges::find_if(table->columns, [&](SqlSchema::Column const& c) { return c.name == columnName; });
    REQUIRE(column != table->columns.end());
    CHECK(column->isPrimaryKey);
    return column->type;
}

/// Asserts the live column type is the 64-bit integer the caller declared.
///
/// SQLite is the one exception: AUTOINCREMENT is only valid on an INTEGER PRIMARY KEY, which is a
/// 64-bit rowid alias there, so the narrower-looking type name carries no loss of range.
void CheckIsSixtyFourBitKey(SqlServerType serverType, SqlColumnTypeDefinition const& type)
{
    using namespace SqlColumnTypeDefinitions;

    if (serverType == SqlServerType::SQLITE)
        CHECK(std::holds_alternative<Integer>(type));
    else
        CHECK(std::holds_alternative<Bigint>(type));
}

} // namespace

// NB: Reflected record types must have external linkage, so this one cannot live in the anonymous
// namespace above.
struct WideKeyedRecord
{
    static constexpr std::string_view TableName = "WideKeyed";

    Field<int64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<32>> name;
};

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlSchema: auto-increment primary key keeps its declared integer width",
                 "[SqlSchema][Migration]")
{
    using namespace SqlColumnTypeDefinitions;

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("WideKeyed")
            .PrimaryKeyWithAutoIncrement("id", Bigint {})
            .RequiredColumn("name", Varchar { 32 });
    });

    CheckIsSixtyFourBitKey(stmt.Connection().ServerType(), LiveColumnType(stmt, "WideKeyed", "id"));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlSchema: DataMapper-created auto-increment primary key keeps its declared integer width",
                 "[SqlSchema][DataMapper][Migration]")
{
    // The reproduction from issue #521: the record declares a 64-bit key, so the created column
    // must be 64-bit wide as well.
    auto dm = DataMapper {};
    dm.CreateTable<WideKeyedRecord>();

    auto stmt = SqlStatement {};
    CheckIsSixtyFourBitKey(stmt.Connection().ServerType(), LiveColumnType(stmt, "WideKeyed", "id"));
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

// ================================================================================================
// ReadAllTables — composite primary keys and multi-column (unique) indexes
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlSchema::ReadAllTables reports composite keys and multi-column indexes", "[SqlSchema]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("SchemaProbe")
            .PrimaryKey("pk_b", SqlColumnTypeDefinitions::Integer {})
            .PrimaryKey("pk_a", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("val_one", SqlColumnTypeDefinitions::Integer {})
            .RequiredColumn("val_two", SqlColumnTypeDefinitions::Integer {});
    });
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateUniqueIndex("SchemaProbe_multi_index", "SchemaProbe", { "val_one", "val_two" });
    });

    auto const tables = SqlSchema::ReadAllTables(stmt, stmt.Connection().DatabaseName(), /*schema=*/"");
    auto const probe = std::ranges::find_if(tables, [](SqlSchema::Table const& t) { return t.name == "SchemaProbe"; });
    REQUIRE(probe != tables.end());

    // Composite primary key columns come back in declaration (key-sequence) order.
    REQUIRE(probe->primaryKeys.size() == 2);
    CHECK(probe->primaryKeys[0] == "pk_b");
    CHECK(probe->primaryKeys[1] == "pk_a");

    // The multi-column unique index is reported with both columns in order and
    // is not confused with the implicit primary-key index.
    auto const index = std::ranges::find_if(probe->indexes, [](auto const& idx) { return idx.columns.size() == 2; });
    REQUIRE(index != probe->indexes.end());
    CHECK(index->columns[0] == "val_one");
    CHECK(index->columns[1] == "val_two");
    CHECK(index->isUnique);
}

// ================================================================================================
// SqlTestFixture::DropTableRecursively — foreign-key graphs that are not trees
// ================================================================================================

namespace
{

SqlSchema::FullyQualifiedTableName TableNamed(std::string name)
{
    return SqlSchema::FullyQualifiedTableName { .catalog = {}, .schema = {}, .table = std::move(name) };
}

/// Builds `CREATE TABLE "<table>" ("id" INT NOT NULL PRIMARY KEY, "ref_<t>" INT REFERENCES "<t>"("id"), ...)`.
/// Plain ANSI DDL, accepted verbatim by SQLite, SQL Server and PostgreSQL alike.
///
/// @param table            Name of the table to create.
/// @param referencedTables Tables to declare an inline foreign key against, one column each.
/// @return The CREATE TABLE statement.
std::string CreateTableReferencingSql(std::string_view table, std::vector<std::string_view> const& referencedTables)
{
    auto sql = std::format(R"(CREATE TABLE "{}" ("id" INT NOT NULL PRIMARY KEY)", table);
    for (auto const& referenced: referencedTables)
        sql += std::format(R"(, "ref_{0}" INT REFERENCES "{0}"("id"))", referenced);
    sql += ')';
    return sql;
}

/// @return Whether @p table is currently present in the connection's default schema.
bool TableExists(SqlStatement& stmt, std::string_view table)
{
    auto const tableNames = SqlTestFixture::GetAllTableNames(stmt);
    return std::ranges::find(tableNames, table) != tableNames.end();
}

/// @return How often @p table appears in @p dropped.
size_t CountDropped(std::vector<SqlSchema::FullyQualifiedTableName> const& dropped, std::string_view table)
{
    return static_cast<size_t>(std::ranges::count_if(dropped, [&](auto const& name) { return name.table == table; }));
}

/// @return The position of @p table within @p dropped; fails the test when it is absent.
size_t DropIndexOf(std::vector<SqlSchema::FullyQualifiedTableName> const& dropped, std::string_view table)
{
    auto const it = std::ranges::find_if(dropped, [&](auto const& name) { return name.table == table; });
    REQUIRE(it != dropped.end());
    return static_cast<size_t>(std::ranges::distance(dropped.begin(), it));
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "SqlTestFixture::DropTableRecursively: self-referencing foreign key", "[SqlSchema]")
{
    auto stmt = SqlStatement {};

    // Chinook's Employee.ReportsTo in miniature: the table is its own dependant. Following that
    // edge blindly recurses until the stack is exhausted.
    (void) stmt.ExecuteDirect(CreateTableReferencingSql("SelfRef", { "SelfRef" }));

    auto const dropped = SqlTestFixture::DropTableRecursively(stmt, TableNamed("SelfRef"));

    CHECK(dropped.size() == 1);
    CHECK(CountDropped(dropped, "SelfRef") == 1);
    CHECK_FALSE(TableExists(stmt, "SelfRef"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlTestFixture::DropTableRecursively: two-table foreign-key cycle", "[SqlSchema]")
{
    auto stmt = SqlStatement {};

    if (stmt.Connection().ServerType() == SqlServerType::SQLITE)
    {
        // SQLite resolves foreign-key targets lazily, so a forward reference is legal here — and
        // it has to be, because SQLite has no ALTER TABLE ... ADD CONSTRAINT to fall back on.
        (void) stmt.ExecuteDirect(CreateTableReferencingSql("CycleA", { "CycleB" }));
        (void) stmt.ExecuteDirect(CreateTableReferencingSql("CycleB", { "CycleA" }));
    }
    else
    {
        (void) stmt.ExecuteDirect(R"(CREATE TABLE "CycleA" ("id" INT NOT NULL PRIMARY KEY, "ref_CycleB" INT))");
        (void) stmt.ExecuteDirect(CreateTableReferencingSql("CycleB", { "CycleA" }));
        (void) stmt.ExecuteDirect(R"(ALTER TABLE "CycleA" ADD CONSTRAINT "FK_CycleA_CycleB" )"
                                  R"(FOREIGN KEY ("ref_CycleB") REFERENCES "CycleB"("id"))");
    }

    auto const dropped = SqlTestFixture::DropTableRecursively(stmt, TableNamed("CycleA"));

    CHECK(dropped.size() == 2);
    CHECK(CountDropped(dropped, "CycleA") == 1);
    CHECK(CountDropped(dropped, "CycleB") == 1);
    CHECK_FALSE(TableExists(stmt, "CycleA"));
    CHECK_FALSE(TableExists(stmt, "CycleB"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlTestFixture::DropTableRecursively: diamond drops each table once", "[SqlSchema]")
{
    auto stmt = SqlStatement {};

    // DiaRoot <- DiaLeft, DiaRight <- DiaLeaf: two independent paths reach DiaLeaf. On top of
    // that DiaLeft references DiaRoot through *two* separate constraints, so DiaRoot's dependant
    // list names DiaLeft twice — the shape that makes the redundant descent observable.
    (void) stmt.ExecuteDirect(CreateTableReferencingSql("DiaRoot", {}));
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "DiaLeft" ("id" INT NOT NULL PRIMARY KEY, )"
                              R"("ref_a" INT REFERENCES "DiaRoot"("id"), )"
                              R"("ref_b" INT REFERENCES "DiaRoot"("id")))");
    (void) stmt.ExecuteDirect(CreateTableReferencingSql("DiaRight", { "DiaRoot" }));
    (void) stmt.ExecuteDirect(CreateTableReferencingSql("DiaLeaf", { "DiaLeft", "DiaRight" }));

    auto const dropped = SqlTestFixture::DropTableRecursively(stmt, TableNamed("DiaRoot"));

    // Four tables, four drops — DiaLeft and DiaLeaf are reached twice each but must be
    // dropped only once.
    CHECK(dropped.size() == 4);
    CHECK(CountDropped(dropped, "DiaLeft") == 1);
    CHECK(CountDropped(dropped, "DiaLeaf") == 1);

    // Dependants are still dropped before what they depend on.
    CHECK(DropIndexOf(dropped, "DiaLeaf") < DropIndexOf(dropped, "DiaLeft"));
    CHECK(DropIndexOf(dropped, "DiaLeaf") < DropIndexOf(dropped, "DiaRight"));
    CHECK(DropIndexOf(dropped, "DiaLeft") < DropIndexOf(dropped, "DiaRoot"));
    CHECK(DropIndexOf(dropped, "DiaRight") < DropIndexOf(dropped, "DiaRoot"));

    CHECK_FALSE(TableExists(stmt, "DiaRoot"));
    CHECK_FALSE(TableExists(stmt, "DiaLeft"));
    CHECK_FALSE(TableExists(stmt, "DiaRight"));
    CHECK_FALSE(TableExists(stmt, "DiaLeaf"));
}
