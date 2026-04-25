// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/UnicodeConverter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include <CodeGenerator.hpp>
#include <LupSqlParser.hpp>
#include <LupVersionConverter.hpp>
#include <SqlStatementParser.hpp>
#include <WhereClauseParser.hpp>

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

TEST_CASE("LupVersion.ToDottedString", "[lup2dbtool]")
{
    // The dotted form matches the convention used by release markers surfaced
    // through dbtool / migrations-gui: no zero-padding, dot-separated segments.
    CHECK(LupVersion { 6, 8, 8 }.ToDottedString() == "6.8.8");
    CHECK(LupVersion { 2, 1, 5 }.ToDottedString() == "2.1.5");
    CHECK(LupVersion { 10, 0, 0 }.ToDottedString() == "10.0.0");
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
    CHECK(ConvertWindows1252ToUtf8("Hello World") == u8"Hello World");
    CHECK(ConvertWindows1252ToUtf8("SELECT * FROM table") == u8"SELECT * FROM table");
    CHECK(ConvertWindows1252ToUtf8("").empty());
}

TEST_CASE("ConvertWindows1252ToUtf8.SpecialCharacters", "[lup2dbtool]")
{
    // Euro sign (0x80) -> U+20AC
    CHECK(ConvertWindows1252ToUtf8("\x80") == u8"\u20AC");

    // Left double quotation mark (0x93) -> U+201C
    CHECK(ConvertWindows1252ToUtf8("\x93") == u8"\u201C");

    // Right double quotation mark (0x94) -> U+201D
    CHECK(ConvertWindows1252ToUtf8("\x94") == u8"\u201D");

    // En dash (0x96) -> U+2013
    CHECK(ConvertWindows1252ToUtf8("\x96") == u8"\u2013");

    // Trade mark sign (0x99) -> U+2122
    CHECK(ConvertWindows1252ToUtf8("\x99") == u8"\u2122");
}

TEST_CASE("ConvertWindows1252ToUtf8.Latin1Supplement", "[lup2dbtool]")
{
    // German umlauts (Latin-1 supplement range 0xA0-0xFF)
    // ä (0xE4) -> U+00E4
    CHECK(ConvertWindows1252ToUtf8("\xE4") == u8"\u00E4");

    // ö (0xF6) -> U+00F6
    CHECK(ConvertWindows1252ToUtf8("\xF6") == u8"\u00F6");

    // ü (0xFC) -> U+00FC
    CHECK(ConvertWindows1252ToUtf8("\xFC") == u8"\u00FC");

    // ß (0xDF) -> U+00DF
    CHECK(ConvertWindows1252ToUtf8("\xDF") == u8"\u00DF");

    // Ä (0xC4) -> U+00C4
    CHECK(ConvertWindows1252ToUtf8("\xC4") == u8"\u00C4");

    // Ö (0xD6) -> U+00D6
    CHECK(ConvertWindows1252ToUtf8("\xD6") == u8"\u00D6");

    // Ü (0xDC) -> U+00DC
    CHECK(ConvertWindows1252ToUtf8("\xDC") == u8"\u00DC");
}

TEST_CASE("ConvertWindows1252ToUtf8.MixedContent", "[lup2dbtool]")
{
    // Mixed ASCII and German text
    // "Prüfung" with ü as 0xFC - use string concatenation to avoid hex escape ambiguity
    CHECK(ConvertWindows1252ToUtf8("Pr\xFC"
                                   "fung")
          == u8"Prüfung");

    // "Größe" with ö as 0xF6 and ß as 0xDF - use string concatenation
    CHECK(ConvertWindows1252ToUtf8("Gr\xF6\xDF"
                                   "e")
          == u8"Größe");

    // "Äpfel" with Ä as 0xC4
    CHECK(ConvertWindows1252ToUtf8("\xC4pfel") == u8"Äpfel");
}

TEST_CASE("ConvertWindows1252ToUtf8.ToStdWideStringChain", "[lup2dbtool]")
{
    // Verify that ToStdWideString(ConvertWindows1252ToUtf8(...)) uses the UTF-8 overload
    // ü (0xFC in Windows-1252) should be correctly converted to wide string
    auto const wideString = Lightweight::ToStdWideString(ConvertWindows1252ToUtf8("\xFC"));
    CHECK(wideString == L"\u00FC");
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

TEST_CASE("ParseSqlStatement.Insert.CommaInsideString", "[lup2dbtool]")
{
    // German decimal notation inside string literal must not split the values.
    auto stmt = ParseSqlStatement("INSERT INTO recipe VALUES (1, '32,5 L', 'x 12,0 32,5 52,5 75')");
    REQUIRE(std::holds_alternative<InsertStmt>(stmt));

    auto const& insert = std::get<InsertStmt>(stmt);
    CHECK(insert.tableName == "recipe");
    REQUIRE(insert.columnValues.size() == 3);
    CHECK(insert.columnValues[0].second == "1");
    CHECK(insert.columnValues[1].second == "'32,5 L'");
    CHECK(insert.columnValues[2].second == "'x 12,0 32,5 52,5 75'");
}

TEST_CASE("ParseSqlStatement.Insert.ParenInsideString", "[lup2dbtool]")
{
    // Parens inside a string literal must not terminate the VALUES clause.
    auto stmt = ParseSqlStatement(
        "INSERT INTO t (id, code, label, status, other_id) "
        "VALUES (16, 'AB/CD', 'primary (unit/legacy)', 'unset', 99)");
    REQUIRE(std::holds_alternative<InsertStmt>(stmt));

    auto const& insert = std::get<InsertStmt>(stmt);
    REQUIRE(insert.columnValues.size() == 5);
    CHECK(insert.columnValues[2].first == "label");
    CHECK(insert.columnValues[2].second == "'primary (unit/legacy)'");
    CHECK(insert.columnValues[4].second == "99");
}

TEST_CASE("ParseSqlStatement.Insert.ParenInsideString.ColumnLessForm", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement(
        "INSERT INTO t VALUES (1, 'a (b) c', 'd,e,f', 2)");
    REQUIRE(std::holds_alternative<InsertStmt>(stmt));

    auto const& insert = std::get<InsertStmt>(stmt);
    REQUIRE(insert.columnValues.size() == 4);
    CHECK(insert.columnValues[1].second == "'a (b) c'");
    CHECK(insert.columnValues[2].second == "'d,e,f'");
}

TEST_CASE("ResolvePositionalInserts substitutes real column names", "[lup2dbtool]")
{
    // Build two migrations: one creates `cfg(nr, val)`, the other inserts into it
    // without an explicit column list — ParseInsert stores "0", "1".
    ParsedMigration m1;
    m1.sourceFile = "m1.sql";
    {
        CreateTableStmt c;
        c.tableName = "cfg";
        c.columns.push_back({ .name = "nr", .type = "integer" });
        c.columns.push_back({ .name = "val", .type = "integer" });
        m1.statements.push_back({ {}, c });
    }

    ParsedMigration m2;
    m2.sourceFile = "m2.sql";
    {
        // Mimic what ParseInsert produces for `INSERT INTO cfg VALUES (4, 215)`
        InsertStmt i;
        i.tableName = "cfg";
        i.columnValues.emplace_back("0", "4");
        i.columnValues.emplace_back("1", "215");
        m2.statements.push_back({ {}, i });
    }

    std::vector migrations { m1, m2 };
    ResolvePositionalInserts(migrations);

    auto const& insert = std::get<InsertStmt>(migrations[1].statements[0].statement);
    REQUIRE(insert.columnValues.size() == 2);
    CHECK(insert.columnValues[0].first == "nr");
    CHECK(insert.columnValues[0].second == "4");
    CHECK(insert.columnValues[1].first == "val");
    CHECK(insert.columnValues[1].second == "215");
}

TEST_CASE("ResolvePositionalInserts tracks ALTER TABLE ADD COLUMN", "[lup2dbtool]")
{
    ParsedMigration m;
    m.sourceFile = "m.sql";
    {
        CreateTableStmt c;
        c.tableName = "t";
        c.columns.push_back({ .name = "id", .type = "integer" });
        m.statements.push_back({ {}, c });
    }
    {
        AlterTableAddColumnStmt a;
        a.tableName = "t";
        a.column.name = "extra";
        a.column.type = "integer";
        m.statements.push_back({ {}, a });
    }
    {
        InsertStmt i;
        i.tableName = "t";
        i.columnValues.emplace_back("0", "1");
        i.columnValues.emplace_back("1", "99");
        m.statements.push_back({ {}, i });
    }

    std::vector migrations { m };
    ResolvePositionalInserts(migrations);

    auto const& insert = std::get<InsertStmt>(migrations[0].statements[2].statement);
    CHECK(insert.columnValues[0].first == "id");
    CHECK(insert.columnValues[1].first == "extra");
}

TEST_CASE("ResolvePositionalInserts leaves out-of-bounds indices untouched", "[lup2dbtool]")
{
    ParsedMigration m;
    m.sourceFile = "m.sql";
    {
        CreateTableStmt c;
        c.tableName = "t";
        c.columns.push_back({ .name = "id", .type = "integer" });
        m.statements.push_back({ {}, c });
    }
    {
        InsertStmt i;
        i.tableName = "t";
        i.columnValues.emplace_back("0", "1");
        i.columnValues.emplace_back("1", "bogus"); // Table has only 1 column.
        m.statements.push_back({ {}, i });
    }

    std::vector migrations { m };
    ResolvePositionalInserts(migrations);

    auto const& insert = std::get<InsertStmt>(migrations[0].statements[1].statement);
    CHECK(insert.columnValues[0].first == "id");   // resolved
    CHECK(insert.columnValues[1].first == "1");    // left as-is (warning printed)
}

TEST_CASE("MapSqlType.ForceUnicodeWidensCharAndVarchar", "[lup2dbtool]")
{
    // Default (forceUnicode=true): CHAR/VARCHAR widen to N-prefixed Unicode variants.
    CHECK(CodeGenerator::MapSqlType("VARCHAR(30)").value_or("?") == "NVarchar(30)");
    CHECK(CodeGenerator::MapSqlType("CHAR(3)").value_or("?") == "NChar(3)");

    // Explicit opt-out: stays as narrow types.
    CHECK(CodeGenerator::MapSqlType("VARCHAR(30)", /*forceUnicode=*/false).value_or("?")
          == "Varchar(30)");
    CHECK(CodeGenerator::MapSqlType("CHAR(3)", /*forceUnicode=*/false).value_or("?") == "Char(3)");

    // Already-unicode types are left alone either way.
    CHECK(CodeGenerator::MapSqlType("NVARCHAR(30)").value_or("?") == "NVarchar(30)");
    CHECK(CodeGenerator::MapSqlType("NCHAR(3)").value_or("?") == "NChar(3)");
    CHECK(CodeGenerator::MapSqlType("NVARCHAR(30)", /*forceUnicode=*/false).value_or("?")
          == "NVarchar(30)");
    CHECK(CodeGenerator::MapSqlType("NCHAR(3)", /*forceUnicode=*/false).value_or("?") == "NChar(3)");

    // Non-char types are unaffected.
    CHECK(CodeGenerator::MapSqlType("INTEGER").value_or("?") == "Integer()");
    CHECK(CodeGenerator::MapSqlType("DECIMAL(10,2)").value_or("?") == "Decimal(10, 2)");
}

TEST_CASE("ParseWhereClause rejects empty input", "[lup2dbtool]")
{
    CHECK(!ParseWhereClause("").has_value());
    CHECK(!ParseWhereClause("   ").has_value());
}

TEST_CASE("ParseWhereClause.SimpleComparison", "[lup2dbtool]")
{
    auto rendered = ParseWhereClause("x = 1");
    REQUIRE(rendered.has_value());
    auto const& text = rendered.value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(text == R"("X" = 1)");
}

TEST_CASE("ParseWhereClause.QualifiedIdentifier", "[lup2dbtool]")
{
    auto rendered = ParseWhereClause("a.col = outer.col");
    REQUIRE(rendered.has_value());
    auto const& text = rendered.value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(text == R"("A"."COL" = "OUTER"."COL")");
}

TEST_CASE("ParseWhereClause.NotEqualNullIsPreservedAsLiteralRhs", "[lup2dbtool]")
{
    // `x <> null` is legal SQL text (though semantically never true); the LUP
    // source uses it as a guard. The parser accepts it and passes it through.
    auto rendered = ParseWhereClause("x <> null");
    REQUIRE(rendered.has_value());
    auto const& text = rendered.value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(text == R"("X" <> NULL)");
}

TEST_CASE("ParseWhereClause.IsNullAndIsNotNull", "[lup2dbtool]")
{
    {
        auto rendered = ParseWhereClause("a.col is null");
        REQUIRE(rendered.has_value());
    auto const& text = rendered.value(); // NOLINT(bugprone-unchecked-optional-access)
        CHECK(text == R"("A"."COL" IS NULL)");
    }
    {
        auto rendered = ParseWhereClause("col IS NOT NULL");
        REQUIRE(rendered.has_value());
    auto const& text = rendered.value(); // NOLINT(bugprone-unchecked-optional-access)
        CHECK(text == R"("COL" IS NOT NULL)");
    }
}

TEST_CASE("ParseWhereClause.NotAndParens", "[lup2dbtool]")
{
    auto rendered = ParseWhereClause("NOT (col is null) AND other = 1");
    REQUIRE(rendered.has_value());
    auto const& text = rendered.value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(text == R"(NOT ("COL" IS NULL) AND "OTHER" = 1)");
}

TEST_CASE("ParseWhereClause.InSubquery", "[lup2dbtool]")
{
    auto rendered = ParseWhereClause("fk IN (select id from parent)");
    REQUIRE(rendered.has_value());
    auto const& text = rendered.value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(text == R"("FK" IN (SELECT "ID" FROM "PARENT"))");
}

TEST_CASE("ParseWhereClause.NotInSubquery", "[lup2dbtool]")
{
    auto rendered = ParseWhereClause("fk not in (select id from parent)");
    REQUIRE(rendered.has_value());
    auto const& text = rendered.value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(text == R"("FK" NOT IN (SELECT "ID" FROM "PARENT"))");
}

TEST_CASE("ParseWhereClause.ExistsCorrelatedSubquery", "[lup2dbtool]")
{
    auto rendered = ParseWhereClause(
        "exists (select * from parent as p where p.id = child.parent_id)");
    REQUIRE(rendered.has_value());
    auto const& text = rendered.value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(text == R"(EXISTS (SELECT * FROM "PARENT" AS "P" WHERE "P"."ID" = "CHILD"."PARENT_ID"))");
}

TEST_CASE("ParseWhereClause.CompositeAndExistsWithIsNull", "[lup2dbtool]")
{
    auto rendered = ParseWhereClause(
        "exists (select * from parent as p where p.id = child.parent_id) AND child.col is null");
    REQUIRE(rendered.has_value());
    auto const& text = rendered.value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(text
          == R"(EXISTS (SELECT * FROM "PARENT" AS "P" WHERE "P"."ID" = "CHILD"."PARENT_ID") AND "CHILD"."COL" IS NULL)");
}

TEST_CASE("ParseWhereClause.RejectsUnknownPredicate", "[lup2dbtool]")
{
    CHECK(!ParseWhereClause("col LIKE 'pattern'").has_value());
    CHECK(!ParseWhereClause("col BETWEEN 1 AND 5").has_value());
}

TEST_CASE("ParseWhereClause.RejectsTrailingGarbage", "[lup2dbtool]")
{
    CHECK(!ParseWhereClause("col = 1 BOGUS").has_value());
}

TEST_CASE("ParseSqlStatement.Update.CompositeWhereIsStructured", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement(
        "UPDATE products SET archived = 1 WHERE (archived <> null) "
        "AND NOT (parent_id IN (select id from parents))");
    REQUIRE(std::holds_alternative<UpdateStmt>(stmt));

    auto const& u = std::get<UpdateStmt>(stmt);
    CHECK(u.tableName == "products");
    REQUIRE(u.setColumns.size() == 1);
    CHECK(u.setColumns[0].first == "archived");
    CHECK(u.setColumns[0].second == "1");
    // Structured parse: whereExpression filled, structured fields cleared.
    CHECK(u.whereColumn.empty());
    CHECK(u.whereExpression
          == R"(("ARCHIVED" <> NULL) AND NOT ("PARENT_ID" IN (SELECT "ID" FROM "PARENTS")))");
}

TEST_CASE("ParseSqlStatement.Update.ExistsSubqueryIsStructured", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement(
        "UPDATE docs SET folder = NAME WHERE EXISTS "
        "(select * from archives as a where a.id=archive_id AND not(a.root_nr is null)) "
        "AND folder is null");
    REQUIRE(std::holds_alternative<UpdateStmt>(stmt));
    auto const& u = std::get<UpdateStmt>(stmt);
    CHECK(u.tableName == "docs");
    CHECK(u.whereExpression.find("EXISTS (") != std::string::npos);
    CHECK(u.whereExpression.find(R"("FOLDER" IS NULL)") != std::string::npos);
}

TEST_CASE("ParseSqlStatement.Update.WhereIsNull", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("UPDATE measurements SET duration = 3 where duration is null");
    REQUIRE(std::holds_alternative<UpdateStmt>(stmt));

    auto const& update = std::get<UpdateStmt>(stmt);
    CHECK(update.tableName == "measurements");
    REQUIRE(update.setColumns.size() == 1);
    CHECK(update.setColumns[0].first == "duration");
    CHECK(update.setColumns[0].second == "3");
    CHECK(update.whereColumn == "duration");
    CHECK(update.whereOp == "IS NULL");
    CHECK(update.whereValue.empty());
}

TEST_CASE("ParseSqlStatement.Update.WhereIsNotNull", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("UPDATE t SET x = 1 WHERE x IS NOT NULL");
    REQUIRE(std::holds_alternative<UpdateStmt>(stmt));
    auto const& u = std::get<UpdateStmt>(stmt);
    CHECK(u.whereColumn == "x");
    CHECK(u.whereOp == "IS NOT NULL");
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

TEST_CASE("ParseSqlStatement.Delete.CompositeAndIsStructured", "[lup2dbtool]")
{
    // The naive `.+` value capture used to slurp `4104 AND idc = 2529` into a
    // single string value. The structured parser now handles composite WHEREs.
    auto stmt = ParseSqlStatement("DELETE FROM dlg_ctrl_prop WHERE idd = 4104 AND idc = 2529");
    REQUIRE(std::holds_alternative<DeleteStmt>(stmt));

    auto const& del = std::get<DeleteStmt>(stmt);
    CHECK(del.tableName == "dlg_ctrl_prop");
    CHECK(del.whereColumn.empty());
    CHECK(del.whereExpression == R"("IDD" = 4104 AND "IDC" = 2529)");
}

TEST_CASE("ParseSqlStatement.Delete.IsNull", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("DELETE FROM cfg WHERE value IS NULL");
    REQUIRE(std::holds_alternative<DeleteStmt>(stmt));
    auto const& del = std::get<DeleteStmt>(stmt);
    CHECK(del.whereColumn == "value");
    CHECK(del.whereOp == "IS NULL");
    CHECK(del.whereExpression.empty());
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

// ================================================================================================
// Multi-word column type parsing (parser must capture the full type phrase)

TEST_CASE("ParseSqlStatement.CreateTable.LongVarcharColumn", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("CREATE TABLE t (id INTEGER NOT NULL, body long varchar)");
    REQUIRE(std::holds_alternative<CreateTableStmt>(stmt));

    auto const& create = std::get<CreateTableStmt>(stmt);
    REQUIRE(create.columns.size() == 2);
    CHECK(create.columns[1].name == "body");
    CHECK(create.columns[1].type == "long varchar");
    CHECK(create.columns[1].isNullable);
}

TEST_CASE("ParseSqlStatement.CreateTable.LongVarcharNotNull", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("CREATE TABLE t (body long varchar not null)");
    REQUIRE(std::holds_alternative<CreateTableStmt>(stmt));

    auto const& create = std::get<CreateTableStmt>(stmt);
    REQUIRE(create.columns.size() == 1);
    CHECK(create.columns[0].type == "long varchar");
    CHECK_FALSE(create.columns[0].isNullable);
}

TEST_CASE("ParseSqlStatement.CreateTable.CollapsesInnerWhitespace", "[lup2dbtool]")
{
    // Inner whitespace in a multi-word type normalizes to single spaces.
    auto stmt = ParseSqlStatement("CREATE TABLE t (body long   varchar)");
    REQUIRE(std::holds_alternative<CreateTableStmt>(stmt));

    auto const& create = std::get<CreateTableStmt>(stmt);
    REQUIRE(create.columns.size() == 1);
    CHECK(create.columns[0].type == "long varchar");
}

TEST_CASE("ParseSqlStatement.CreateTable.DecimalKeepsParenContents", "[lup2dbtool]")
{
    // Paren contents must stay atomic — the comma inside decimal(10,2) must NOT be
    // treated as a column separator, and the type phrase must include the full parens.
    auto stmt = ParseSqlStatement("CREATE TABLE t (amount decimal(10,2) not null)");
    REQUIRE(std::holds_alternative<CreateTableStmt>(stmt));

    auto const& create = std::get<CreateTableStmt>(stmt);
    REQUIRE(create.columns.size() == 1);
    CHECK(create.columns[0].type == "decimal(10,2)");
    CHECK_FALSE(create.columns[0].isNullable);
}

TEST_CASE("ParseSqlStatement.CreateTable.TinyintMoney", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("CREATE TABLE t (flag tinyint null, price money not null)");
    REQUIRE(std::holds_alternative<CreateTableStmt>(stmt));

    auto const& create = std::get<CreateTableStmt>(stmt);
    REQUIRE(create.columns.size() == 2);
    CHECK(create.columns[0].type == "tinyint");
    CHECK(create.columns[0].isNullable);
    CHECK(create.columns[1].type == "money");
    CHECK_FALSE(create.columns[1].isNullable);
}

TEST_CASE("ParseSqlStatement.AlterTableAddColumn.LongVarchar", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("alter table T add column NOTES long varchar");
    REQUIRE(std::holds_alternative<AlterTableAddColumnStmt>(stmt));

    auto const& alter = std::get<AlterTableAddColumnStmt>(stmt);
    CHECK(alter.tableName == "T");
    CHECK(alter.column.name == "NOTES");
    CHECK(alter.column.type == "long varchar");
    CHECK(alter.column.isNullable);
}

TEST_CASE("ParseSqlStatement.AlterTableAddColumn.MoneyNotNull", "[lup2dbtool]")
{
    auto stmt = ParseSqlStatement("alter table T add column PRICE money not null");
    REQUIRE(std::holds_alternative<AlterTableAddColumnStmt>(stmt));

    auto const& alter = std::get<AlterTableAddColumnStmt>(stmt);
    CHECK(alter.column.name == "PRICE");
    CHECK(alter.column.type == "money");
    CHECK_FALSE(alter.column.isNullable);
}

// ================================================================================================
// CodeGenerator::MapSqlType — SQL type to Lightweight DSL mapping

TEST_CASE("MapSqlType.SingleWordTypes", "[lup2dbtool]")
{
    CHECK(*CodeGenerator::MapSqlType("integer") == "Integer()");
    CHECK(*CodeGenerator::MapSqlType("INTEGER") == "Integer()");
    CHECK(*CodeGenerator::MapSqlType("smallint") == "Smallint()");
    CHECK(*CodeGenerator::MapSqlType("bigint") == "Bigint()");
    CHECK(*CodeGenerator::MapSqlType("text") == "Text()");
}

TEST_CASE("MapSqlType.Tinyint", "[lup2dbtool]")
{
    CHECK(*CodeGenerator::MapSqlType("tinyint") == "Tinyint()");
    CHECK(*CodeGenerator::MapSqlType("TINYINT") == "Tinyint()");
}

TEST_CASE("MapSqlType.Money", "[lup2dbtool]")
{
    // SQL Server `money` is internally decimal(19,4). Matches LUpd's Oracle/DB2/PostgreSQL mapping.
    CHECK(*CodeGenerator::MapSqlType("money") == "Decimal(19, 4)");
    CHECK(*CodeGenerator::MapSqlType("MONEY") == "Decimal(19, 4)");
}

TEST_CASE("MapSqlType.LongVarchar", "[lup2dbtool]")
{
    CHECK(*CodeGenerator::MapSqlType("long varchar") == "Text()");
    CHECK(*CodeGenerator::MapSqlType("LONG VARCHAR") == "Text()");
    CHECK(*CodeGenerator::MapSqlType("Long VarChar") == "Text()");
}

TEST_CASE("MapSqlType.LongVarcharWithExtraWhitespace", "[lup2dbtool]")
{
    // Internal whitespace runs should canonicalize to the single-space form.
    CHECK(*CodeGenerator::MapSqlType("long  varchar") == "Text()");
    CHECK(*CodeGenerator::MapSqlType("long\tvarchar") == "Text()");
    CHECK(*CodeGenerator::MapSqlType("  long varchar  ") == "Text()");
}

TEST_CASE("MapSqlType.LongVarbinary", "[lup2dbtool]")
{
    CHECK(*CodeGenerator::MapSqlType("long varbinary") == "VarBinary()");
    CHECK(*CodeGenerator::MapSqlType("LONG VARBINARY") == "VarBinary()");
}

TEST_CASE("MapSqlType.ParameterizedTypesStillWork", "[lup2dbtool]")
{
    // Default forceUnicode=true widens char/varchar; pass false to check the narrow path.
    CHECK(*CodeGenerator::MapSqlType("varchar(50)", /*forceUnicode=*/false) == "Varchar(50)");
    CHECK(*CodeGenerator::MapSqlType("char(30)", /*forceUnicode=*/false) == "Char(30)");
    CHECK(*CodeGenerator::MapSqlType("decimal(10,2)") == "Decimal(10, 2)");
    CHECK(*CodeGenerator::MapSqlType("decimal(10)") == "Decimal(10)");
}

TEST_CASE("MapSqlType.UnknownTypeFails", "[lup2dbtool]")
{
    // Types we don't know about return an error rather than silently mapping.
    CHECK_FALSE(CodeGenerator::MapSqlType("someweirdthing").has_value());
    CHECK_FALSE(CodeGenerator::MapSqlType("enum('a','b')").has_value());
}

// ================================================================================================
// Plugin CMakeLists.txt / Plugin.cpp emission

TEST_CASE("GeneratePluginCMake.UsesPluginName", "[lup2dbtool]")
{
    std::ostringstream out;
    CodeGenerator::GeneratePluginCMake(out, "MyMigrations");
    auto const script = out.str();

    // Plugin name appears everywhere a target name would: add_library, GLOB variable, link, properties.
    CHECK(script.find("add_library(MyMigrations MODULE") != std::string::npos);
    CHECK(script.find("target_link_libraries(MyMigrations PRIVATE Lightweight::Lightweight") != std::string::npos);
    CHECK(script.find("${MyMigrations_MIGRATIONS}") != std::string::npos);
    // Plugin.cpp is the entry point the script must compile alongside the generated sources.
    CHECK(script.find("Plugin.cpp") != std::string::npos);
    // CONFIGURE_DEPENDS glob keeps lup_*.cpp additions cheap.
    CHECK(script.find("CONFIGURE_DEPENDS") != std::string::npos);
    CHECK(script.find("lup_*.cpp") != std::string::npos);
    // Output destination matches the existing plugin convention.
    CHECK(script.find("${CMAKE_BINARY_DIR}/plugins") != std::string::npos);
}

TEST_CASE("GeneratePluginCMake.DefaultPluginName", "[lup2dbtool]")
{
    std::ostringstream out;
    CodeGenerator::GeneratePluginCMake(out, "LupMigrations");
    auto const script = out.str();
    CHECK(script.find("add_library(LupMigrations MODULE") != std::string::npos);
}

TEST_CASE("GeneratePluginEntryPoint.DeclaresPluginMacro", "[lup2dbtool]")
{
    std::ostringstream out;
    CodeGenerator::GeneratePluginEntryPoint(out);
    auto const src = out.str();

    CHECK(src.find("#include <Lightweight/SqlMigration.hpp>") != std::string::npos);
    CHECK(src.find("LIGHTWEIGHT_MIGRATION_PLUGIN()") != std::string::npos);
}

// ================================================================================================
// ParseFilename — development sentinel for upd_m_next_release

TEST_CASE("ParseFilename.NextReleaseSentinel", "[lup2dbtool]")
{
    auto const v = ParseFilename("upd_m_next_release.sql");
    REQUIRE(v.has_value());
    CHECK(v->major == 9999); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(v->minor == 99);   // NOLINT(bugprone-unchecked-optional-access)
    CHECK(v->patch == 99);   // NOLINT(bugprone-unchecked-optional-access)
}

TEST_CASE("ParseFilename.NextReleaseSentinelWithoutExtension", "[lup2dbtool]")
{
    auto const v = ParseFilename("upd_m_next_release");
    REQUIRE(v.has_value());
    CHECK(v->major == 9999); // NOLINT(bugprone-unchecked-optional-access)
}

TEST_CASE("ParseFilename.NextReleaseSortsAfterRealVersions", "[lup2dbtool]")
{
    auto const sentinel = ParseFilename("upd_m_next_release.sql");
    REQUIRE(sentinel.has_value());
    // Must sort strictly greater than any realistic future version.
    CHECK(LupVersion { 99, 99, 99 } < *sentinel); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(LupVersion { 10, 0, 0 } < *sentinel);   // NOLINT(bugprone-unchecked-optional-access)
}
