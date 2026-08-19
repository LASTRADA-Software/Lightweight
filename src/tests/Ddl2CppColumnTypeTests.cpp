// SPDX-License-Identifier: Apache-2.0

// End-to-end coverage for the SQL-type -> C++-type mapping that `ddl2cpp` performs.
//
// The tests here deliberately do *not* speak SQL. A hand-written DDL script can only ever be run
// against the one dialect it was written for, which is why this coverage used to be limited to a
// hard-coded SQLite schema. Instead, every probe table is created through the dialect-agnostic
// migration query builder, read back out of the *live* database catalog via
// `SqlSchema::ReadAllTables`, and only then handed to `CxxModelPrinter::MakeType` — the very
// function `ddl2cpp` emits its record members with. That closes the loop the unit tests in
// `CxxModelPrinterTests.cpp` leave open: those feed `MakeType` a hand-written `SqlSchema::Column`,
// so they cannot notice a column type that fails to survive the DDL or the catalog round trip.
//
// Where a DBMS legitimately stores something other than what was asked for, it is listed as a
// `DialectException` carrying the reason. That list is a large part of the point of these tests:
// it is the only place the per-DBMS column-type divergences are written down and continuously
// verified.

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/Tools/CxxModelPrinter.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using namespace Lightweight;
using Lightweight::Tools::CxxModelPrinter;

namespace
{

/// A DBMS that maps a declared column type onto a C++ type other than the expected one.
struct DialectException
{
    /// The DBMS that deviates.
    SqlServerType serverType {};
    /// The C++ type `ddl2cpp` emits on that DBMS.
    std::string_view cxxType {};
    /// Why the deviation happens — and whether it is correct.
    std::string_view reason {};
};

/// One declared SQL column type and the C++ type `ddl2cpp` must generate for it.
struct ColumnTypeCase
{
    /// Column name used in the probe table.
    std::string_view columnName {};
    /// Type handed to the migration query builder.
    SqlColumnTypeDefinition declaredType {};
    /// C++ type expected on every DBMS not listed in @ref dialectExceptions.
    std::string_view expectedCxxType {};
    /// DBMS-specific deviations from @ref expectedCxxType.
    std::vector<DialectException> dialectExceptions {};

    /// @return The deviation recorded for @p serverType, or nullptr when it maps as expected.
    [[nodiscard]] DialectException const* ExceptionFor(SqlServerType serverType) const
    {
        auto const exception =
            std::ranges::find_if(dialectExceptions, [=](auto const& e) { return e.serverType == serverType; });
        return exception != dialectExceptions.end() ? &*exception : nullptr;
    }

    /// @return The C++ type expected on @p serverType.
    [[nodiscard]] std::string_view ExpectedFor(SqlServerType serverType) const
    {
        auto const* exception = ExceptionFor(serverType);
        return exception != nullptr ? exception->cxxType : expectedCxxType;
    }
};

/// @return Failure context naming the column under test and, where one applies, the dialect
///         deviation that makes the expectation differ on @p serverType.
///
/// Built unconditionally on purpose: `INFO` declares a `Catch::ScopedMessage`, so writing it as the
/// substatement of an `if` would destroy the message at the end of that `if` — long before the
/// assertion it is meant to annotate.
[[nodiscard]] std::string FailureContext(ColumnTypeCase const& testCase, SqlServerType serverType)
{
    auto const* const exception = testCase.ExceptionFor(serverType);
    if (exception == nullptr)
        return std::format("column {}", testCase.columnName);
    return std::format("column {} (dialect exception: {})", testCase.columnName, exception->reason);
}

// The PostgreSQL Unicode ODBC driver reports *every* character column as its wide ODBC counterpart
// (SQL_WCHAR / SQL_WVARCHAR), because PostgreSQL stores all text as UTF-8 and the driver hands it
// over as UTF-16. Non-Unicode and Unicode declarations therefore converge on the same C++ type.
constexpr std::string_view PostgresReportsTextAsUnicode =
    "The PostgreSQL Unicode driver reports every character column as its wide ODBC type";

// SQLite is dynamically typed and has a single TEXT storage class: it neither pads CHAR(n) to a
// fixed width nor distinguishes Unicode from non-Unicode text, so both the fixed-width trimming
// and the UTF-16 distinction are lost on the way back out of the catalog.
constexpr std::string_view SqliteHasOneTextType =
    "SQLite has a single dynamically typed TEXT storage class: no CHAR padding, no Unicode variant";

/// The column types every supported DBMS must round-trip, mirroring issue #191's DDL script.
///
/// The columns are declared NOT NULL so each expectation reads as the bare C++ type; nullability is
/// covered separately below.
std::vector<ColumnTypeCase> const& ColumnTypeCases()
{
    using namespace SqlColumnTypeDefinitions;

    static std::vector<ColumnTypeCase> const cases {
        // ------------------------------------------------------------------ integral types
        { .columnName = "tinyIntColumn",
          .declaredType = Tinyint {},
          .expectedCxxType = "uint8_t",
          .dialectExceptions = { { .serverType = SqlServerType::POSTGRESQL,
                                   .cxxType = "int16_t",
                                   .reason = "PostgreSQL has no 1-byte integer; Tinyint is emitted as SMALLINT" } } },
        { .columnName = "smallIntColumn", .declaredType = Smallint {}, .expectedCxxType = "int16_t" },
        { .columnName = "integerColumn", .declaredType = Integer {}, .expectedCxxType = "int32_t" },
        { .columnName = "bigIntColumn", .declaredType = Bigint {}, .expectedCxxType = "int64_t" },

        // ------------------------------------------------------- fixed-point and floating-point
        { .columnName = "numericColumn",
          .declaredType = Decimal { .precision = 10, .scale = 2 },
          .expectedCxxType = "Light::SqlNumeric<10, 2>" },

        // A 4-byte REAL comes back as `double` on SQLite and MS SQL Server: the schema reader
        // deliberately collapses every column whose dialect type name is `float` or `real` to
        // Real{53} (see the fixup in SqlSchema.cpp), which widens rather than loses data.
        // PostgreSQL names its 4-byte type `float4`, which that fixup does not match, so the true
        // width survives there — PostgreSQL is the accurate one here.
        { .columnName = "realColumn",
          .declaredType = Real { .precision = 24 },
          .expectedCxxType = "double",
          .dialectExceptions = { { .serverType = SqlServerType::POSTGRESQL,
                                   .cxxType = "float",
                                   .reason = "PostgreSQL reports float4 with its true 4-byte width" } } },

        // The same fixup is what makes an 8-byte column correct on SQLite and MS SQL Server. On
        // PostgreSQL the dialect type name is `float8`, the fixup misses it, and the driver's
        // reported width narrows the column to `float`.
        //
        // KNOWN DEFECT: `double precision` on PostgreSQL must generate `double`. `float` here is a
        // silent 8-to-4-byte narrowing in generated records. Asserted as-is so the behaviour is
        // recorded rather than unnoticed; update this entry (do not add a new exception) once the
        // reader's float fixup learns PostgreSQL's float4/float8 type names.
        { .columnName = "doubleColumn",
          .declaredType = Real { .precision = 53 },
          .expectedCxxType = "double",
          .dialectExceptions = { { .serverType = SqlServerType::POSTGRESQL,
                                   .cxxType = "float",
                                   .reason = "KNOWN DEFECT: float8 is narrowed to float32 by the schema reader" } } },

        // ------------------------------------------------------------------------- boolean
        { .columnName = "booleanColumn", .declaredType = Bool {}, .expectedCxxType = "bool" },

        // -------------------------------------------------------------------- date and time
        { .columnName = "dateColumn", .declaredType = Date {}, .expectedCxxType = "Light::SqlDate" },
        { .columnName = "timeColumn", .declaredType = Time {}, .expectedCxxType = "Light::SqlTime" },
        { .columnName = "datetimeColumn", .declaredType = DateTime {}, .expectedCxxType = "Light::SqlDateTime" },

        // ------------------------------------------------------------------ non-Unicode text
        { .columnName = "charColumn",
          .declaredType = Char { .size = 8 },
          .expectedCxxType = "Light::SqlTrimmedFixedString<8>",
          .dialectExceptions = { { .serverType = SqlServerType::SQLITE,
                                   .cxxType = "Light::SqlAnsiString<8>",
                                   .reason = SqliteHasOneTextType },
                                 { .serverType = SqlServerType::POSTGRESQL,
                                   .cxxType = "Light::SqlTrimmedFixedString<8, wchar_t>",
                                   .reason = PostgresReportsTextAsUnicode } } },
        { .columnName = "varcharColumn",
          .declaredType = Varchar { .size = 30 },
          .expectedCxxType = "Light::SqlAnsiString<30>",
          .dialectExceptions = { { .serverType = SqlServerType::POSTGRESQL,
                                   .cxxType = "Light::SqlDynamicUtf16String<30>",
                                   .reason = PostgresReportsTextAsUnicode } } },

        // ------------------------------------------------------------------------- Unicode text
        { .columnName = "nCharColumn",
          .declaredType = NChar { .size = 8 },
          .expectedCxxType = "Light::SqlTrimmedFixedString<8, wchar_t>",
          .dialectExceptions = { { .serverType = SqlServerType::SQLITE,
                                   .cxxType = "Light::SqlAnsiString<8>",
                                   .reason = SqliteHasOneTextType } } },
        { .columnName = "nVarCharColumn",
          .declaredType = NVarchar { .size = 30 },
          .expectedCxxType = "Light::SqlDynamicUtf16String<30>",
          .dialectExceptions = { { .serverType = SqlServerType::SQLITE,
                                   .cxxType = "Light::SqlAnsiString<30>",
                                   .reason = SqliteHasOneTextType } } },

        // ------------------------------------------------------------------------------ binary
        // `Binary{n}` is a fixed-width request that no supported backend actually honours: the
        // SQLite formatter emits BLOB, the SQL Server formatter emits VARBINARY(n), and the
        // PostgreSQL formatter emits the inherently unsized BYTEA.
        { .columnName = "binaryColumn",
          .declaredType = Binary { .size = 16 },
          .expectedCxxType = "Light::SqlBinary",
          .dialectExceptions = { { .serverType = SqlServerType::MICROSOFT_SQL,
                                   .cxxType = "Light::SqlDynamicBinary<16>",
                                   .reason = "the SQL Server formatter emits VARBINARY(n) for Binary{n}" },
                                 { .serverType = SqlServerType::POSTGRESQL,
                                   .cxxType = "Light::SqlDynamicBinary<0>",
                                   .reason = "PostgreSQL BYTEA carries no declared length" } } },
        { .columnName = "varBinaryColumn",
          .declaredType = VarBinary { .size = 16 },
          .expectedCxxType = "Light::SqlDynamicBinary<16>",
          .dialectExceptions = { { .serverType = SqlServerType::POSTGRESQL,
                                   .cxxType = "Light::SqlDynamicBinary<0>",
                                   .reason = "PostgreSQL BYTEA carries no declared length" } } },
    };
    return cases;
}

/// Reads @p tableName back out of the live database catalog.
SqlSchema::Table ReadTable(SqlStatement& stmt, std::string_view tableName)
{
    auto const tables = SqlSchema::ReadAllTables(stmt, stmt.Connection().DatabaseName(), /*schema=*/"");
    auto const table = std::ranges::find_if(tables, [&](SqlSchema::Table const& t) { return t.name == tableName; });
    REQUIRE(table != tables.end());
    return *table;
}

/// @return The column named @p columnName of @p table.
SqlSchema::Column const& ColumnOf(SqlSchema::Table const& table, std::string_view columnName)
{
    auto const column =
        std::ranges::find_if(table.columns, [&](SqlSchema::Column const& c) { return c.name == columnName; });
    REQUIRE(column != table.columns.end());
    return *column;
}

/// @return The C++ type `ddl2cpp` generates for @p column with its default settings.
std::string GeneratedCxxType(SqlSchema::Column const& column, std::string const& tableName)
{
    return CxxModelPrinter::MakeType(column, tableName, /*forceUnicodeTextColumn=*/false, {}, SqlOptimalMaxColumnSize);
}

/// Creates a single-column table holding @p testCase's primary key, reads it back, and checks both
/// that the catalog reports it as the primary key and that `ddl2cpp` generates the expected type.
void CheckPrimaryKeyColumnType(SqlStatement& stmt, std::string_view tableName, ColumnTypeCase const& testCase)
{
    stmt.MigrateDirect([&](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable(tableName).PrimaryKey(std::string(testCase.columnName), testCase.declaredType);
    });

    auto const serverType = stmt.Connection().ServerType();
    INFO(FailureContext(testCase, serverType));
    auto const table = ReadTable(stmt, tableName);
    auto const& key = ColumnOf(table, testCase.columnName);
    CHECK(key.isPrimaryKey);
    CHECK_FALSE(key.isNullable);
    CHECK(GeneratedCxxType(key, std::string(tableName)) == testCase.ExpectedFor(serverType));
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "ddl2cpp: SQL column types map to their documented C++ types", "[ddl2cpp][SqlSchema]")
{
    auto stmt = SqlStatement {};
    auto const serverType = stmt.Connection().ServerType();

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        auto table = migration.CreateTable("ColumnTypeTest1");
        table.PrimaryKeyWithAutoIncrement("integerAutoincrementPK");
        for (auto const& testCase: ColumnTypeCases())
            table.RequiredColumn(std::string(testCase.columnName), testCase.declaredType);
    });

    auto const table = ReadTable(stmt, "ColumnTypeTest1");

    for (auto const& testCase: ColumnTypeCases())
    {
        INFO(FailureContext(testCase, serverType));
        auto const& column = ColumnOf(table, testCase.columnName);
        CHECK_FALSE(column.isNullable);
        CHECK(GeneratedCxxType(column, "ColumnTypeTest1") == testCase.ExpectedFor(serverType));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "ddl2cpp: a nullable column becomes std::optional", "[ddl2cpp][SqlSchema]")
{
    using namespace SqlColumnTypeDefinitions;

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("ColumnTypeNullability")
            .PrimaryKeyWithAutoIncrement("id")
            .RequiredColumn("integerNotNull", Integer {})
            .Column("integerNull", Integer {});
    });

    auto const table = ReadTable(stmt, "ColumnTypeNullability");

    auto const& notNull = ColumnOf(table, "integerNotNull");
    CHECK_FALSE(notNull.isNullable);
    CHECK(GeneratedCxxType(notNull, "ColumnTypeNullability") == "int32_t");

    auto const& nullable = ColumnOf(table, "integerNull");
    CHECK(nullable.isNullable);
    CHECK(GeneratedCxxType(nullable, "ColumnTypeNullability") == "std::optional<int32_t>");
}

TEST_CASE_METHOD(SqlTestFixture, "ddl2cpp: an auto-increment primary key is reported as such", "[ddl2cpp][SqlSchema]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("ColumnTypeAutoIncrementKey").PrimaryKeyWithAutoIncrement("integerAutoincrementPK");
    });

    auto const table = ReadTable(stmt, "ColumnTypeAutoIncrementKey");
    REQUIRE(table.primaryKeys.size() == 1);
    CHECK(table.primaryKeys.front() == "integerAutoincrementPK");

    auto const& key = ColumnOf(table, "integerAutoincrementPK");
    CHECK(key.isPrimaryKey);
    CHECK_FALSE(key.isNullable);
}

TEST_CASE_METHOD(SqlTestFixture, "ddl2cpp: a GUID primary key maps to SqlGuid", "[ddl2cpp][SqlSchema]")
{
    using namespace SqlColumnTypeDefinitions;

    auto stmt = SqlStatement {};
    CheckPrimaryKeyColumnType(
        stmt,
        "ColumnTypeTest2",
        // SQLite has no GUID type at all: the declared `GUID` is an unrecognised type name that the
        // driver reports back as a VARCHAR of unknown length, so the generated member degenerates
        // to an unsized dynamic string rather than SqlGuid.
        ColumnTypeCase { .columnName = "guidPK",
                         .declaredType = Guid {},
                         .expectedCxxType = "Light::SqlGuid",
                         .dialectExceptions = { { .serverType = SqlServerType::SQLITE,
                                                  .cxxType = "Light::SqlDynamicAnsiString<0>",
                                                  .reason = "SQLite has no GUID type; the column reads back as an "
                                                            "unsized VARCHAR" } } });
}

TEST_CASE_METHOD(SqlTestFixture, "ddl2cpp: a VARCHAR primary key keeps its declared length", "[ddl2cpp][SqlSchema]")
{
    using namespace SqlColumnTypeDefinitions;

    auto stmt = SqlStatement {};
    CheckPrimaryKeyColumnType(stmt,
                              "ColumnTypeTest3",
                              ColumnTypeCase { .columnName = "varcharPK",
                                               .declaredType = Varchar { .size = 30 },
                                               .expectedCxxType = "Light::SqlAnsiString<30>",
                                               .dialectExceptions = { { .serverType = SqlServerType::POSTGRESQL,
                                                                        .cxxType = "Light::SqlDynamicUtf16String<30>",
                                                                        .reason = PostgresReportsTextAsUnicode } } });
}

// TEXT and TIMESTAMP are kept out of the shared probe table: each declaration means something
// materially different per dialect, so a surprise on one backend must not mask the mapping of every
// other column.

TEST_CASE_METHOD(SqlTestFixture, "ddl2cpp: an unbounded TEXT column maps to a dynamic string", "[ddl2cpp][SqlSchema]")
{
    using namespace SqlColumnTypeDefinitions;

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("ColumnTypeText").PrimaryKeyWithAutoIncrement("id").RequiredColumn("textColumn", Text {});
    });

    auto const table = ReadTable(stmt, "ColumnTypeText");
    auto const generated = GeneratedCxxType(ColumnOf(table, "textColumn"), "ColumnTypeText");

    switch (stmt.Connection().ServerType())
    {
        case SqlServerType::MICROSOFT_SQL:
            // VARCHAR(MAX): the driver reports the maximum character count, which is exactly the
            // marker `MakeType` turns into the unbounded string type.
            CHECK(generated == "Light::SqlMaxDynamicAnsiString");
            break;
        case SqlServerType::SQLITE:
            // SQLite reports its TEXT storage class without a length, so the member degenerates to
            // an unsized dynamic string rather than the unbounded one.
            CHECK(generated == "Light::SqlDynamicAnsiString<0>");
            break;
        case SqlServerType::POSTGRESQL:
            // PostgreSQL TEXT is unbounded, but the Unicode driver substitutes its configured
            // MaxLongVarcharSize for the unknown length. That number is a driver setting rather
            // than a fact about the column, so only the shape is asserted here.
            CHECK_THAT(generated, Catch::Matchers::StartsWith("Light::SqlDynamicUtf16String<"));
            break;
        default:
            FAIL("Unhandled server type");
    }
}

TEST_CASE_METHOD(SqlTestFixture, "ddl2cpp: a TIMESTAMP column maps to SqlDateTime", "[ddl2cpp][SqlSchema]")
{
    using namespace SqlColumnTypeDefinitions;

    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable("ColumnTypeTimestamp")
            .PrimaryKeyWithAutoIncrement("id")
            .RequiredColumn("timestampColumn", Timestamp {});
    });

    auto const table = ReadTable(stmt, "ColumnTypeTimestamp");

    // KNOWN DEFECT on MS SQL Server: `TIMESTAMP` there is a synonym for `rowversion` — an 8-byte,
    // server-generated, non-writable binary counter, not a point in time. The SQL Server formatter
    // emits it verbatim for `Timestamp{}`, so the catalog reports binary(8) and ddl2cpp generates a
    // binary member. Asserted as-is so the behaviour is recorded rather than unnoticed; replacing
    // the emitted type (DATETIME2 being the natural candidate) is a formatter change of its own.
    auto const expected = stmt.Connection().ServerType() == SqlServerType::MICROSOFT_SQL
                              ? std::string_view { "Light::SqlDynamicBinary<8>" }
                              : std::string_view { "Light::SqlDateTime" };
    CHECK(GeneratedCxxType(ColumnOf(table, "timestampColumn"), "ColumnTypeTimestamp") == expected);
}
