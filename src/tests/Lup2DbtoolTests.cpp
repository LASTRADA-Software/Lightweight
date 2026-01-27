// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <Lightweight/DataBinder/UnicodeConverter.hpp>
#include <LupSqlParser.hpp>
#include <LupVersionConverter.hpp>
#include <SqlStatementParser.hpp>

using namespace Lup2DbTool;
using Lightweight::ConvertWindows1252ToUtf8;

// ================================================================================================
// LupVersion Tests

TEST_CASE("LupVersion.ToString", "[lup2dbtool]")
{
    CHECK(LupVersion { 6, 8, 8 }.ToString() == "6_08_08");
    CHECK(LupVersion { 2, 1, 5 }.ToString() == "2_01_05");
    CHECK(LupVersion { 10, 0, 0 }.ToString() == "10_00_00");
}

TEST_CASE("LupVersion.ToInteger", "[lup2dbtool]")
{
    // For versions >= 6.0.0: major * 10000 + minor * 100 + patch
    CHECK(LupVersion { 6, 8, 8 }.ToInteger() == 60808);
    CHECK(LupVersion { 10, 0, 0 }.ToInteger() == 100000);

    // For versions < 6.0.0: major * 100 + minor * 10 + patch
    CHECK(LupVersion { 2, 1, 5 }.ToInteger() == 215);
    CHECK(LupVersion { 3, 0, 0 }.ToInteger() == 300);
}

TEST_CASE("LupVersion.ToMigrationTimestamp", "[lup2dbtool]")
{
    CHECK(LupVersion { 6, 8, 8 }.ToMigrationTimestamp() == 20000000060808ULL);
    CHECK(LupVersion { 2, 1, 5 }.ToMigrationTimestamp() == 20000000000215ULL);
}

TEST_CASE("LupVersion.Comparison", "[lup2dbtool]")
{
    CHECK(LupVersion { 2, 1, 5 } < LupVersion { 2, 1, 6 });
    CHECK(LupVersion { 2, 1, 6 } < LupVersion { 2, 2, 0 });
    CHECK(LupVersion { 2, 2, 0 } < LupVersion { 3, 0, 0 });
    CHECK(LupVersion { 6, 8, 8 } == LupVersion { 6, 8, 8 });
}

// ================================================================================================
// ConvertWindows1252ToUtf8 Tests

TEST_CASE("ConvertWindows1252ToUtf8.AsciiPassthrough", "[lup2dbtool]")
{
    // ASCII characters (0x00-0x7F) should pass through unchanged
    CHECK(ConvertWindows1252ToUtf8("Hello World") == "Hello World");
    CHECK(ConvertWindows1252ToUtf8("SELECT * FROM table") == "SELECT * FROM table");
    CHECK(ConvertWindows1252ToUtf8("").empty());
}

TEST_CASE("ConvertWindows1252ToUtf8.SpecialCharacters", "[lup2dbtool]")
{
    // Euro sign (0x80) -> UTF-8: E2 82 AC
    CHECK(ConvertWindows1252ToUtf8("\x80") == "\xE2\x82\xAC");

    // Left double quotation mark (0x93) -> UTF-8: E2 80 9C
    CHECK(ConvertWindows1252ToUtf8("\x93") == "\xE2\x80\x9C");

    // Right double quotation mark (0x94) -> UTF-8: E2 80 9D
    CHECK(ConvertWindows1252ToUtf8("\x94") == "\xE2\x80\x9D");

    // En dash (0x96) -> UTF-8: E2 80 93
    CHECK(ConvertWindows1252ToUtf8("\x96") == "\xE2\x80\x93");

    // Trade mark sign (0x99) -> UTF-8: E2 84 A2
    CHECK(ConvertWindows1252ToUtf8("\x99") == "\xE2\x84\xA2");
}

TEST_CASE("ConvertWindows1252ToUtf8.Latin1Supplement", "[lup2dbtool]")
{
    // German umlauts (Latin-1 supplement range 0xA0-0xFF)
    // ä (0xE4) -> UTF-8: C3 A4
    CHECK(ConvertWindows1252ToUtf8("\xE4") == "\xC3\xA4");

    // ö (0xF6) -> UTF-8: C3 B6
    CHECK(ConvertWindows1252ToUtf8("\xF6") == "\xC3\xB6");

    // ü (0xFC) -> UTF-8: C3 BC
    CHECK(ConvertWindows1252ToUtf8("\xFC") == "\xC3\xBC");

    // ß (0xDF) -> UTF-8: C3 9F
    CHECK(ConvertWindows1252ToUtf8("\xDF") == "\xC3\x9F");

    // Ä (0xC4) -> UTF-8: C3 84
    CHECK(ConvertWindows1252ToUtf8("\xC4") == "\xC3\x84");

    // Ö (0xD6) -> UTF-8: C3 96
    CHECK(ConvertWindows1252ToUtf8("\xD6") == "\xC3\x96");

    // Ü (0xDC) -> UTF-8: C3 9C
    CHECK(ConvertWindows1252ToUtf8("\xDC") == "\xC3\x9C");
}

TEST_CASE("ConvertWindows1252ToUtf8.MixedContent", "[lup2dbtool]")
{
    // Mixed ASCII and German text
    // "Prüfung" with ü as 0xFC - use string concatenation to avoid hex escape ambiguity
    CHECK(ConvertWindows1252ToUtf8("Pr\xFC" "fung") == "Prüfung");

    // "Größe" with ö as 0xF6 and ß as 0xDF - use string concatenation
    CHECK(ConvertWindows1252ToUtf8("Gr\xF6\xDF" "e") == "Größe");

    // "Äpfel" with Ä as 0xC4
    CHECK(ConvertWindows1252ToUtf8("\xC4pfel") == "Äpfel");
}

// ================================================================================================
// ParseFilename Tests

TEST_CASE("ParseFilename.InitMigration", "[lup2dbtool]")
{
    auto const v = ParseFilename("init_m_2_1_5.sql");
    REQUIRE(v.has_value());
    CHECK((*v).major == 2); // NOLINT(bugprone-unchecked-optional-access)
    CHECK((*v).minor == 1); // NOLINT(bugprone-unchecked-optional-access)
    CHECK((*v).patch == 5); // NOLINT(bugprone-unchecked-optional-access)
}

TEST_CASE("ParseFilename.UpdateMigration", "[lup2dbtool]")
{
    auto const v = ParseFilename("upd_m_6_08_08.sql");
    REQUIRE(v.has_value());
    CHECK((*v).major == 6); // NOLINT(bugprone-unchecked-optional-access)
    CHECK((*v).minor == 8); // NOLINT(bugprone-unchecked-optional-access)
    CHECK((*v).patch == 8); // NOLINT(bugprone-unchecked-optional-access)
}

TEST_CASE("ParseFilename.RangeMigration", "[lup2dbtool]")
{
    // Range migrations like upd_m_2_1_6__2_1_9.sql use the end version
    auto const v = ParseFilename("upd_m_2_1_6__2_1_9.sql");
    REQUIRE(v.has_value());
    CHECK((*v).major == 2); // NOLINT(bugprone-unchecked-optional-access)
    CHECK((*v).minor == 1); // NOLINT(bugprone-unchecked-optional-access)
    CHECK((*v).patch == 9); // NOLINT(bugprone-unchecked-optional-access)
}

TEST_CASE("ParseFilename.InvalidFilename", "[lup2dbtool]")
{
    CHECK_FALSE(ParseFilename("random_file.sql").has_value());
    CHECK_FALSE(ParseFilename("update_m_1_2_3.sql").has_value());
    CHECK_FALSE(ParseFilename("init_m_1_2.sql").has_value());
}

TEST_CASE("IsInitMigration", "[lup2dbtool]")
{
    CHECK(IsInitMigration("init_m_2_1_5.sql"));
    CHECK_FALSE(IsInitMigration("upd_m_6_08_08.sql"));
    CHECK_FALSE(IsInitMigration("random.sql"));
}

TEST_CASE("IsUpdateMigration", "[lup2dbtool]")
{
    CHECK(IsUpdateMigration("upd_m_6_08_08.sql"));
    CHECK(IsUpdateMigration("upd_m_2_1_6__2_1_9.sql"));
    CHECK_FALSE(IsUpdateMigration("init_m_2_1_5.sql"));
    CHECK_FALSE(IsUpdateMigration("random.sql"));
}

// ================================================================================================
// SqlStatementParser Tests

TEST_CASE("ParseSqlStatement.CreateTable", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("CREATE TABLE test (id INTEGER PRIMARY KEY, name VARCHAR(50) NOT NULL)");
    REQUIRE(std::holds_alternative<CreateTableStmt>(stmt));

    auto const& create = std::get<CreateTableStmt>(stmt);
    CHECK(create.tableName == "test");
    REQUIRE(create.columns.size() == 2);

    CHECK(create.columns[0].name == "id");
    CHECK(create.columns[0].type == "INTEGER");
    CHECK(create.columns[0].isPrimaryKey);

    CHECK(create.columns[1].name == "name");
    CHECK(create.columns[1].type == "VARCHAR(50)");
    CHECK_FALSE(create.columns[1].isNullable);
}

TEST_CASE("ParseSqlStatement.AlterTableAddColumn", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("ALTER TABLE test ADD COLUMN value DOUBLE NULL");
    REQUIRE(std::holds_alternative<AlterTableAddColumnStmt>(stmt));

    auto const& alter = std::get<AlterTableAddColumnStmt>(stmt);
    CHECK(alter.tableName == "test");
    CHECK(alter.column.name == "value");
    CHECK(alter.column.type == "DOUBLE");
    CHECK(alter.column.isNullable);
}

TEST_CASE("ParseSqlStatement.Insert", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("INSERT INTO users (name, age) VALUES ('John', 30)");
    REQUIRE(std::holds_alternative<InsertStmt>(stmt));

    auto const& insert = std::get<InsertStmt>(stmt);
    CHECK(insert.tableName == "users");
    REQUIRE(insert.columnValues.size() == 2);
    CHECK(insert.columnValues[0].first == "name");
    CHECK(insert.columnValues[0].second == "'John'");
    CHECK(insert.columnValues[1].first == "age");
    CHECK(insert.columnValues[1].second == "30");
}

TEST_CASE("ParseSqlStatement.Update", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("UPDATE config SET value = 100 WHERE key = 'test'");
    REQUIRE(std::holds_alternative<UpdateStmt>(stmt));

    auto const& update = std::get<UpdateStmt>(stmt);
    CHECK(update.tableName == "config");
    REQUIRE(update.setColumns.size() == 1);
    CHECK(update.setColumns[0].first == "value");
    CHECK(update.setColumns[0].second == "100");
    CHECK(update.whereColumn == "key");
    CHECK(update.whereOp == "=");
    CHECK(update.whereValue == "'test'");
}

TEST_CASE("ParseSqlStatement.Delete", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("DELETE FROM temp WHERE id = 5");
    REQUIRE(std::holds_alternative<DeleteStmt>(stmt));

    auto const& del = std::get<DeleteStmt>(stmt);
    CHECK(del.tableName == "temp");
    CHECK(del.whereColumn == "id");
    CHECK(del.whereOp == "=");
    CHECK(del.whereValue == "5");
}

TEST_CASE("ParseSqlStatement.CreateIndex", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("CREATE INDEX idx_user_email ON Users (email)");
    REQUIRE(std::holds_alternative<CreateIndexStmt>(stmt));

    auto const& idx = std::get<CreateIndexStmt>(stmt);
    CHECK(idx.indexName == "idx_user_email");
    CHECK(idx.tableName == "Users");
    REQUIRE(idx.columns.size() == 1);
    CHECK(idx.columns[0] == "email");
    CHECK_FALSE(idx.unique);
}

TEST_CASE("ParseSqlStatement.CreateUniqueIndex", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("CREATE UNIQUE INDEX idx_user_username ON Users (username)");
    REQUIRE(std::holds_alternative<CreateIndexStmt>(stmt));

    auto const& idx = std::get<CreateIndexStmt>(stmt);
    CHECK(idx.indexName == "idx_user_username");
    CHECK(idx.tableName == "Users");
    REQUIRE(idx.columns.size() == 1);
    CHECK(idx.columns[0] == "username");
    CHECK(idx.unique);
}

TEST_CASE("ParseSqlStatement.CreateIndexMultipleColumns", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("CREATE INDEX idx_user_name ON Users (first_name, last_name)");
    REQUIRE(std::holds_alternative<CreateIndexStmt>(stmt));

    auto const& idx = std::get<CreateIndexStmt>(stmt);
    CHECK(idx.indexName == "idx_user_name");
    CHECK(idx.tableName == "Users");
    REQUIRE(idx.columns.size() == 2);
    CHECK(idx.columns[0] == "first_name");
    CHECK(idx.columns[1] == "last_name");
    CHECK_FALSE(idx.unique);
}

TEST_CASE("IsCreateIndex", "[lup2dbtool]")
{
    CHECK(IsCreateIndex("CREATE INDEX idx_test ON test (col)"));
    CHECK(IsCreateIndex("CREATE UNIQUE INDEX idx_test ON test (col)"));
    CHECK(IsCreateIndex("  CREATE INDEX idx_test ON test (col)"));
    CHECK_FALSE(IsCreateIndex("ALTER TABLE test ADD INDEX (col)"));
    CHECK_FALSE(IsCreateIndex("CREATE TABLE test (id INT)"));
}

TEST_CASE("ParseSqlStatement.DropTable", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("DROP TABLE test_table");
    REQUIRE(std::holds_alternative<DropTableStmt>(stmt));

    auto const& drop = std::get<DropTableStmt>(stmt);
    CHECK(drop.tableName == "test_table");
}

TEST_CASE("ParseSqlStatement.DropTableCaseInsensitive", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("drop table MY_TABLE");
    REQUIRE(std::holds_alternative<DropTableStmt>(stmt));

    auto const& drop = std::get<DropTableStmt>(stmt);
    CHECK(drop.tableName == "MY_TABLE");
}

TEST_CASE("ParseSqlStatement.AlterTableAddForeignKey", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("ALTER TABLE orders ADD FOREIGN KEY (customer_id) REFERENCES customers(id)");
    REQUIRE(std::holds_alternative<AlterTableAddForeignKeyStmt>(stmt));

    auto const& alter = std::get<AlterTableAddForeignKeyStmt>(stmt);
    CHECK(alter.tableName == "orders");
    CHECK(alter.foreignKey.columnName == "customer_id");
    CHECK(alter.foreignKey.referencedTable == "customers");
    CHECK(alter.foreignKey.referencedColumn == "id");
}

TEST_CASE("ParseSqlStatement.AlterTableAddCompositeForeignKey", "[lup2dbtool]")
{
    auto stmt =
        ParseSqlStatement("ALTER TABLE order_items ADD FOREIGN KEY (order_id, product_id) REFERENCES catalog(oid, pid)");
    REQUIRE(std::holds_alternative<AlterTableAddCompositeForeignKeyStmt>(stmt));

    auto const& alter = std::get<AlterTableAddCompositeForeignKeyStmt>(stmt);
    CHECK(alter.tableName == "order_items");
    REQUIRE(alter.columns.size() == 2);
    CHECK(alter.columns[0] == "order_id");
    CHECK(alter.columns[1] == "product_id");
    CHECK(alter.referencedTable == "catalog");
    REQUIRE(alter.referencedColumns.size() == 2);
    CHECK(alter.referencedColumns[0] == "oid");
    CHECK(alter.referencedColumns[1] == "pid");
}

TEST_CASE("ParseSqlStatement.AlterTableDropForeignKey", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("ALTER TABLE orders DROP FOREIGN KEY (customer_id) REFERENCES customers(id)");
    REQUIRE(std::holds_alternative<AlterTableDropForeignKeyStmt>(stmt));

    auto const& alter = std::get<AlterTableDropForeignKeyStmt>(stmt);
    CHECK(alter.tableName == "orders");
    CHECK(alter.columnName == "customer_id");
    CHECK(alter.referencedTable == "customers");
    CHECK(alter.referencedColumn == "id");
}

TEST_CASE("ParseSqlStatement.NormalizesWhitespace", "[lup2dbtool]")
{
    // Multiple spaces between keywords should be normalized
    auto stmt = ParseSqlStatement("ALTER    TABLE   test    ADD    COLUMN   value   INTEGER   NULL");
    REQUIRE(std::holds_alternative<AlterTableAddColumnStmt>(stmt));

    auto const& alter = std::get<AlterTableAddColumnStmt>(stmt);
    CHECK(alter.tableName == "test");
    CHECK(alter.column.name == "value");
    CHECK(alter.column.type == "INTEGER");
}

TEST_CASE("ParseSqlStatement.ForeignKeyWithSpaces", "[lup2dbtool]")
{
    // Foreign key with spaces around parentheses (as in LUP SQL files)
    auto stmt = ParseSqlStatement("ALTER TABLE ZEM_PROBE ADD FOREIGN KEY ( ZUSATZSTOFF_NR ) REFERENCES BETON ( NR )");
    REQUIRE(std::holds_alternative<AlterTableAddForeignKeyStmt>(stmt));

    auto const& alter = std::get<AlterTableAddForeignKeyStmt>(stmt);
    CHECK(alter.tableName == "ZEM_PROBE");
    CHECK(alter.foreignKey.columnName == "ZUSATZSTOFF_NR");
    CHECK(alter.foreignKey.referencedTable == "BETON");
    CHECK(alter.foreignKey.referencedColumn == "NR");
}

TEST_CASE("IsDropTable", "[lup2dbtool]")
{
    CHECK(IsDropTable("DROP TABLE test"));
    CHECK(IsDropTable("drop table test"));
    CHECK(IsDropTable("  DROP TABLE test"));
    CHECK_FALSE(IsDropTable("CREATE TABLE test (id INT)"));
    CHECK_FALSE(IsDropTable("ALTER TABLE test DROP COLUMN col"));
}

TEST_CASE("ParseSqlStatement.RawSql", "[lup2dbtool]")
{
    // Unrecognized statements become RawSqlStmt
    auto stmt = ParseSqlStatement("TRUNCATE TABLE test");
    REQUIRE(std::holds_alternative<RawSqlStmt>(stmt));

    auto const& raw = std::get<RawSqlStmt>(stmt);
    CHECK(raw.sql == "TRUNCATE TABLE test");
}
