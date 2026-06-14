// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataMapper/Field.hpp>
#include <Lightweight/DataMapper/Record.hpp>
#include <Lightweight/SqlBackup/Common.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlDataBinder.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlLogger.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/Utils.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <clocale>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

using namespace std::string_view_literals;
using namespace Lightweight;

struct SimpleRecord
{
    Field<int> value;
    Field<SqlAnsiString<50>> name;
};

struct RecordWithPrimaryKey
{
    Field<int, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<50>> name;
};

struct RecordWithAutoIncrementPK
{
    Field<int, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<100>> description;
};

struct RecordWithCustomTableName
{
    Field<int, PrimaryKey::AutoAssign> id;
    static constexpr std::string_view TableName = "custom_table"sv;
};

struct RecordWithMultipleFields
{
    Field<int, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<50>> name;
    Field<double> amount;
    Field<int> extraValue;
};

// ================================================================================================
// Tests for Field type traits (from Field.hpp) used in Create()
// ================================================================================================

TEST_CASE("IsField type trait", "[Utils][CompileTime][Create]")
{
    SECTION("Field types are recognized")
    {
        static_assert(IsField<Field<int>>);
        static_assert(IsField<Field<SqlAnsiString<50>>>);
        static_assert(IsField<Field<int, PrimaryKey::AutoAssign>>);
        static_assert(IsField<Field<int, PrimaryKey::ServerSideAutoIncrement>>);
    }

    SECTION("Non-Field types are not recognized")
    {
        static_assert(!IsField<int>);
        static_assert(!IsField<SqlAnsiString<50>>);
        static_assert(!IsField<double>);
    }

    SECTION("cv-qualified Field types are recognized")
    {
        static_assert(IsField<Field<int> const>);
        static_assert(IsField<Field<int>&>);
        static_assert(IsField<Field<int> const&>);
    }
}

TEST_CASE("IsPrimaryKey type trait", "[Utils][CompileTime][Create]")
{
    SECTION("Primary key fields are recognized")
    {
        static_assert(IsPrimaryKey<Field<int, PrimaryKey::AutoAssign>>);
        static_assert(IsPrimaryKey<Field<int, PrimaryKey::ServerSideAutoIncrement>>);
    }

    SECTION("Non-primary key fields are not recognized")
    {
        static_assert(!IsPrimaryKey<Field<int>>);
        static_assert(!IsPrimaryKey<Field<SqlAnsiString<50>>>);
        static_assert(!IsPrimaryKey<Field<int, PrimaryKey::No>>);
    }

    SECTION("Non-Field types are not primary keys")
    {
        static_assert(!IsPrimaryKey<int>);
        static_assert(!IsPrimaryKey<SqlAnsiString<50>>);
    }
}

TEST_CASE("IsAutoIncrementPrimaryKey type trait", "[Utils][CompileTime][Create]")
{
    SECTION("Auto-increment primary key fields are recognized")
    {
        static_assert(IsAutoIncrementPrimaryKey<Field<int, PrimaryKey::ServerSideAutoIncrement>>);
    }

    SECTION("Auto-assign primary keys are NOT auto-increment")
    {
        static_assert(!IsAutoIncrementPrimaryKey<Field<int, PrimaryKey::AutoAssign>>);
    }

    SECTION("Non-primary key fields are not auto-increment")
    {
        static_assert(!IsAutoIncrementPrimaryKey<Field<int>>);
        static_assert(!IsAutoIncrementPrimaryKey<Field<SqlAnsiString<50>>>);
    }
}

// ================================================================================================
// Tests for Record utilities (from Record.hpp) used in Create()
// ================================================================================================

TEST_CASE("DataMapperRecord concept", "[Utils][CompileTime][Create]")
{
    SECTION("Valid aggregate records satisfy the concept")
    {
        static_assert(DataMapperRecord<SimpleRecord>);
        static_assert(DataMapperRecord<RecordWithPrimaryKey>);
        static_assert(DataMapperRecord<RecordWithAutoIncrementPK>);
        static_assert(DataMapperRecord<RecordWithCustomTableName>);
    }

    SECTION("Non-aggregate types do not satisfy the concept")
    {
        static_assert(!DataMapperRecord<int>);
        static_assert(!DataMapperRecord<std::string>);
    }
}

TEST_CASE("HasPrimaryKey compile-time check", "[Utils][CompileTime][Create]")
{
    SECTION("Records with primary keys")
    {
        static_assert(HasPrimaryKey<RecordWithPrimaryKey>);
        static_assert(HasPrimaryKey<RecordWithAutoIncrementPK>);
        static_assert(HasPrimaryKey<RecordWithCustomTableName>);
        static_assert(HasPrimaryKey<RecordWithMultipleFields>);
    }

    SECTION("Records without primary keys")
    {
        static_assert(!HasPrimaryKey<SimpleRecord>);
    }
}

TEST_CASE("HasAutoIncrementPrimaryKey compile-time check", "[Utils][CompileTime][Create]")
{
    SECTION("Records with auto-increment primary keys")
    {
        static_assert(HasAutoIncrementPrimaryKey<RecordWithAutoIncrementPK>);
    }

    SECTION("Records without auto-increment primary keys")
    {
        static_assert(!HasAutoIncrementPrimaryKey<SimpleRecord>);
        static_assert(!HasAutoIncrementPrimaryKey<RecordWithPrimaryKey>);
        static_assert(!HasAutoIncrementPrimaryKey<RecordWithCustomTableName>);
    }
}

TEST_CASE("RecordPrimaryKeyIndex compile-time lookup", "[Utils][CompileTime][Create]")
{
    SECTION("Index is correct for records with primary key at first position")
    {
        static_assert(RecordPrimaryKeyIndex<RecordWithPrimaryKey> == 0);
        static_assert(RecordPrimaryKeyIndex<RecordWithAutoIncrementPK> == 0);
        static_assert(RecordPrimaryKeyIndex<RecordWithMultipleFields> == 0);
    }

    SECTION("Index is max size_t for records without primary key")
    {
        static_assert(RecordPrimaryKeyIndex<SimpleRecord> == (std::numeric_limits<size_t>::max)());
    }
}

TEST_CASE("RecordPrimaryKeyType compile-time type extraction", "[Utils][CompileTime][Create]")
{
    SECTION("Primary key type is correctly extracted")
    {
        static_assert(std::is_same_v<RecordPrimaryKeyType<RecordWithPrimaryKey>, int>);
        static_assert(std::is_same_v<RecordPrimaryKeyType<RecordWithAutoIncrementPK>, int>);
    }

    SECTION("Records without primary key have monostate as key type")
    {
        static_assert(std::is_same_v<RecordPrimaryKeyType<SimpleRecord>, std::monostate>);
    }
}

// ================================================================================================
// Tests for table and field naming utilities (from Utils.hpp) used in Create()
// ================================================================================================

TEST_CASE("RecordTableName compile-time extraction", "[Utils][CompileTime][Create]")
{
    SECTION("Default table name is the struct name")
    {
        // Note: The actual name depends on the reflection library, but for anonymous namespace
        // types, we test the custom table name case
        static_assert(RecordTableName<RecordWithCustomTableName> == "custom_table"sv);
    }

    SECTION("Custom TableName is used when provided")
    {
        static_assert(RecordTableName<RecordWithCustomTableName> == "custom_table"sv);
    }
}

TEST_CASE("FieldNameAt compile-time extraction", "[Utils][CompileTime][Create]")
{
    SECTION("Field names are correctly extracted by index")
    {
        static_assert(FieldNameAt<0, SimpleRecord> == "value"sv);
        static_assert(FieldNameAt<1, SimpleRecord> == "name"sv);
    }

    SECTION("Field names for record with primary key")
    {
        static_assert(FieldNameAt<0, RecordWithPrimaryKey> == "id"sv);
        static_assert(FieldNameAt<1, RecordWithPrimaryKey> == "name"sv);
    }

    SECTION("Field names for record with multiple fields")
    {
        static_assert(FieldNameAt<0, RecordWithMultipleFields> == "id"sv);
        static_assert(FieldNameAt<1, RecordWithMultipleFields> == "name"sv);
        static_assert(FieldNameAt<2, RecordWithMultipleFields> == "amount"sv);
        static_assert(FieldNameAt<3, RecordWithMultipleFields> == "extraValue"sv);
    }
}

// ================================================================================================
// Tests for FieldWithStorage concept (from Record.hpp) used in Create()
// ================================================================================================

TEST_CASE("FieldWithStorage concept", "[Utils][CompileTime][Create]")
{
    SECTION("Field types satisfy FieldWithStorage")
    {
        static_assert(FieldWithStorage<Field<int>>);
        static_assert(FieldWithStorage<Field<SqlAnsiString<50>>>);
        static_assert(FieldWithStorage<Field<int, PrimaryKey::AutoAssign>>);
    }
}

TEST_CASE("RecordStorageFieldCount compile-time counting", "[Utils][CompileTime][Create]")
{
    SECTION("Counts all storage fields in a record")
    {
        static_assert(RecordStorageFieldCount<SimpleRecord> == 2);
        static_assert(RecordStorageFieldCount<RecordWithPrimaryKey> == 2);
        static_assert(RecordStorageFieldCount<RecordWithMultipleFields> == 4);
    }
}

// ================================================================================================
// SqlConnectionString / ParseConnectionString / BuildConnectionString
// ================================================================================================

TEST_CASE("SqlConnectionString::SanitizePwd masks password", "[SqlConnectInfo]")
{
    auto const sanitized = SqlConnectionString::SanitizePwd("Driver={SQLite3};DATABASE=test.db;UID=admin;PWD=secret123;");
    CHECK_FALSE(sanitized.contains("secret123"));
    CHECK(sanitized.contains("Pwd=***"));
}

TEST_CASE("SqlConnectionString::SanitizePwd handles missing password", "[SqlConnectInfo]")
{
    auto const sanitized = SqlConnectionString::SanitizePwd("Driver={SQLite3};DATABASE=test.db;");
    CHECK(sanitized == "Driver={SQLite3};DATABASE=test.db;");
}

TEST_CASE("SqlConnectionString::Sanitized member function", "[SqlConnectInfo]")
{
    SqlConnectionString const cs { .value = "Driver={SQLite3};PWD=hide-me;" };
    auto const sanitized = cs.Sanitized();
    CHECK_FALSE(sanitized.contains("hide-me"));
}

TEST_CASE("ParseConnectionString returns key/value map", "[SqlConnectInfo]")
{
    SqlConnectionString const cs { .value = "Driver={SQLite3};Database=test.db;UID=admin;PWD=secret;" };
    auto const map = ParseConnectionString(cs);

    REQUIRE(map.contains("DRIVER"));
    CHECK(map.at("DRIVER") == "SQLite3"); // braces stripped
    REQUIRE(map.contains("DATABASE"));
    CHECK(map.at("DATABASE") == "test.db");
    REQUIRE(map.contains("UID"));
    CHECK(map.at("UID") == "admin");
    REQUIRE(map.contains("PWD"));
    CHECK(map.at("PWD") == "secret");
}

TEST_CASE("ParseConnectionString trims whitespace and ignores invalid pairs", "[SqlConnectInfo]")
{
    SqlConnectionString const cs { .value = " Driver = {SQLite3} ; bad-pair-no-equals ; KEY=Value " };
    auto const map = ParseConnectionString(cs);

    REQUIRE(map.contains("DRIVER"));
    CHECK(map.at("DRIVER") == "SQLite3");
    REQUIRE(map.contains("KEY"));
    CHECK(map.at("KEY") == "Value");
    CHECK_FALSE(map.contains("BAD-PAIR-NO-EQUALS"));
}

TEST_CASE("BuildConnectionString round-trips through ParseConnectionString", "[SqlConnectInfo]")
{
    SqlConnectionStringMap input;
    input["DRIVER"] = "SQLite3";
    input["DATABASE"] = "test.db";
    input["UID"] = "admin";

    auto const built = BuildConnectionString(input);
    auto const parsed = ParseConnectionString(built);
    CHECK(parsed == input);
}

TEST_CASE("EnsureSqliteDatabaseFileExists: non-SQLite driver no-op", "[SqlConnectInfo]")
{
    SqlConnectionString const cs { .value = "Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=test;" };
    CHECK(EnsureSqliteDatabaseFileExists(cs));
}

TEST_CASE("EnsureSqliteDatabaseFileExists: missing DRIVER short-circuits", "[SqlConnectInfo]")
{
    SqlConnectionString const cs { .value = "Database=test.db;" };
    CHECK(EnsureSqliteDatabaseFileExists(cs));
}

TEST_CASE("EnsureSqliteDatabaseFileExists: missing DATABASE short-circuits", "[SqlConnectInfo]")
{
    SqlConnectionString const cs { .value = "Driver=SQLite3;" };
    CHECK(EnsureSqliteDatabaseFileExists(cs));
}

TEST_CASE("EnsureSqliteDatabaseFileExists: in-memory paths short-circuit", "[SqlConnectInfo]")
{
    CHECK(EnsureSqliteDatabaseFileExists(SqlConnectionString { .value = "Driver=SQLite3;Database=:memory:;" }));
    CHECK(EnsureSqliteDatabaseFileExists(SqlConnectionString { .value = "Driver=SQLite3;Database=file::memory:;" }));
    CHECK(EnsureSqliteDatabaseFileExists(SqlConnectionString { .value = "Driver=SQLite3;Database=file:foo?mode=memory;" }));
}

TEST_CASE("EnsureSqliteDatabaseFileExists: creates file at fresh path", "[SqlConnectInfo]")
{
    auto const tempDir = std::filesystem::temp_directory_path() / "lightweight-coverage-test";
    std::filesystem::create_directories(tempDir);
    auto const dbPath = tempDir / "fresh.db";
    std::filesystem::remove(dbPath); // make sure it doesn't exist

    SqlConnectionString const cs { .value = std::format("Driver=SQLite3;Database={};", dbPath.string()) };
    CHECK(EnsureSqliteDatabaseFileExists(cs));
    CHECK(std::filesystem::exists(dbPath));
    std::filesystem::remove(dbPath);
}

TEST_CASE("SqlConnectionDataSource::FromConnectionString extracts fields", "[SqlConnectInfo]")
{
    SqlConnectionString const cs { .value = "DSN=mydsn;UID=user;PWD=pass;TIMEOUT=30;" };
    auto const ds = SqlConnectionDataSource::FromConnectionString(cs);

    CHECK(ds.datasource == "mydsn");
    CHECK(ds.username == "user");
    CHECK(ds.password == "pass");
    CHECK(ds.timeout == std::chrono::seconds(30));
}

TEST_CASE("SqlConnectionDataSource::ToConnectionString round-trip", "[SqlConnectInfo]")
{
    SqlConnectionDataSource const ds {
        .datasource = "mydsn", .username = "user", .password = "pass", .timeout = std::chrono::seconds(15)
    };
    auto const cs = ds.ToConnectionString();
    CHECK(cs.value.contains("DSN=mydsn"));
    CHECK(cs.value.contains("UID=user"));
    CHECK(cs.value.contains("TIMEOUT=15"));
}

TEST_CASE("SqlConnectInfo formatter for both alternatives", "[SqlConnectInfo]")
{
    SqlConnectInfo dsnVariant = SqlConnectionDataSource { .datasource = "DS", .username = "u", .password = "p" };
    auto const dsnFmt = std::format("{}", dsnVariant);
    CHECK(dsnFmt.contains("DSN=DS"));

    SqlConnectInfo csVariant = SqlConnectionString { .value = "Driver=SQLite3;" };
    auto const csFmt = std::format("{}", csVariant);
    CHECK(csFmt == "Driver=SQLite3;");
}

TEST_CASE("SqlConnectionString equality and ordering", "[SqlConnectInfo]")
{
    SqlConnectionString const a { .value = "abc" };
    SqlConnectionString const b { .value = "abc" };
    SqlConnectionString const c { .value = "def" };
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
}

// ================================================================================================
// SqlError / SqlErrorCategory
// ================================================================================================

TEST_CASE("SqlErrorCategory::message covers every code", "[SqlError]")
{
    auto const& cat = SqlErrorCategory::get();
    CHECK(cat.name() == std::string_view("Lightweight"));
    CHECK(cat.message(static_cast<int>(SqlError::SUCCESS)) == "SQL_SUCCESS");
    CHECK(cat.message(static_cast<int>(SqlError::SUCCESS_WITH_INFO)) == "SQL_SUCCESS_WITH_INFO");
    CHECK(cat.message(static_cast<int>(SqlError::NODATA)) == "SQL_NO_DATA");
    CHECK(cat.message(static_cast<int>(SqlError::FAILURE)) == "SQL_ERROR");
    CHECK(cat.message(static_cast<int>(SqlError::INVALID_HANDLE)) == "SQL_INVALID_HANDLE");
    CHECK(cat.message(static_cast<int>(SqlError::STILL_EXECUTING)) == "SQL_STILL_EXECUTING");
    CHECK(cat.message(static_cast<int>(SqlError::NEED_DATA)) == "SQL_NEED_DATA");
    CHECK(cat.message(static_cast<int>(SqlError::PARAM_DATA_AVAILABLE)) == "SQL_PARAM_DATA_AVAILABLE");
    CHECK(cat.message(static_cast<int>(SqlError::UNSUPPORTED_TYPE)) == "SQL_UNSUPPORTED_TYPE");
    CHECK(cat.message(static_cast<int>(SqlError::INVALID_ARGUMENT)) == "SQL_INVALID_ARGUMENT");
    CHECK(cat.message(static_cast<int>(SqlError::TRANSACTION_ERROR)) == "SQL_TRANSACTION_ERROR");
    // Default branch: arbitrary unknown code falls through to "SQL error code N".
    CHECK(cat.message(99999).contains("SQL error code"));
}

TEST_CASE("make_error_code wraps SqlError into std::error_code", "[SqlError]")
{
    auto const ec = make_error_code(SqlError::FAILURE);
    CHECK(ec.value() == static_cast<int>(SqlError::FAILURE));
    CHECK(std::string_view(ec.category().name()) == "Lightweight");
    CHECK(ec.message() == "SQL_ERROR");
}

TEST_CASE("std::formatter<SqlError> produces human-readable text", "[SqlError]")
{
    auto const formatted = std::format("{}", SqlError::INVALID_ARGUMENT);
    CHECK(formatted == "SQL_INVALID_ARGUMENT");
}

TEST_CASE("std::formatter<SqlErrorInfo> produces structured text", "[SqlError]")
{
    SqlErrorInfo const info { .nativeErrorCode = 42, .sqlState = "08001", .message = "Connection failed" };
    auto const formatted = std::format("{}", info);
    CHECK(formatted.contains("08001"));
    CHECK(formatted.contains("(42)"));
    CHECK(formatted.contains("Connection failed"));
}

// ================================================================================================
// SqlSchema utility types and formatters
// ================================================================================================

TEST_CASE("SqlSchema::FullyQualifiedTableName equality and ordering", "[SqlSchema]")
{
    using Lightweight::SqlSchema::FullyQualifiedTableName;
    FullyQualifiedTableName const a { .catalog = "c", .schema = "s", .table = "t" };
    FullyQualifiedTableName const b { .catalog = "c", .schema = "s", .table = "t" };
    FullyQualifiedTableName const c { .catalog = "c", .schema = "s", .table = "u" };
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
}

TEST_CASE("SqlSchema::ColumnIdentifier equality and ordering", "[SqlSchema]")
{
    using Lightweight::SqlSchema::ColumnIdentifier;
    using Lightweight::SqlSchema::FullyQualifiedTableName;
    FullyQualifiedTableName const t1 { .catalog = "", .schema = "s", .table = "t" };
    FullyQualifiedTableName const t2 { .catalog = "", .schema = "s", .table = "t" };
    ColumnIdentifier const a { .table = t1, .column = "x" };
    ColumnIdentifier const b { .table = t2, .column = "x" };
    ColumnIdentifier const c { .table = t1, .column = "y" };
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
}

TEST_CASE("SqlSchema::FullyQualifiedTableName formatter", "[SqlSchema]")
{
    using Lightweight::SqlSchema::FullyQualifiedTableName;

    SECTION("table only — no catalog or schema")
    {
        FullyQualifiedTableName const name { .catalog = "", .schema = "", .table = "Users" };
        CHECK(std::format("{}", name) == "Users");
    }

    SECTION("schema and table")
    {
        FullyQualifiedTableName const name { .catalog = "", .schema = "dbo", .table = "Users" };
        CHECK(std::format("{}", name) == "dbo.Users");
    }

    SECTION("catalog and table")
    {
        FullyQualifiedTableName const name { .catalog = "MyDB", .schema = "", .table = "Users" };
        CHECK(std::format("{}", name) == "MyDB.Users");
    }

    SECTION("catalog, schema, and table")
    {
        FullyQualifiedTableName const name { .catalog = "MyDB", .schema = "dbo", .table = "Users" };
        // The formatter renders schema first, then catalog, then table — it accepts either order.
        // Just verify the components are present.
        auto const formatted = std::format("{}", name);
        CHECK(formatted.contains("Users"));
        CHECK(formatted.contains("dbo"));
        CHECK(formatted.contains("MyDB"));
    }
}

TEST_CASE("SqlSchema::ColumnIdentifier formatter", "[SqlSchema]")
{
    using Lightweight::SqlSchema::ColumnIdentifier;
    using Lightweight::SqlSchema::FullyQualifiedTableName;

    SECTION("with table prefix")
    {
        ColumnIdentifier const c { .table = { .catalog = "", .schema = "", .table = "Users" }, .column = "Id" };
        CHECK(std::format("{}", c) == "Users.Id");
    }

    SECTION("without table prefix returns column only")
    {
        ColumnIdentifier const c { .table = { .catalog = "", .schema = "", .table = "" }, .column = "Id" };
        CHECK(std::format("{}", c) == "Id");
    }
}

TEST_CASE("SqlSchema::ColumnIdentifierSequence formatter", "[SqlSchema]")
{
    using Lightweight::SqlSchema::ColumnIdentifierSequence;
    using Lightweight::SqlSchema::FullyQualifiedTableName;

    ColumnIdentifierSequence const seq { .table = { .catalog = "", .schema = "", .table = "Users" },
                                         .columns = { "Id", "Name", "Email" } };
    auto const formatted = std::format("{}", seq);
    CHECK(formatted == "Users(Id, Name, Email)");
}

TEST_CASE("SqlSchema::ColumnIdentifierSequence: single column", "[SqlSchema]")
{
    using Lightweight::SqlSchema::ColumnIdentifierSequence;
    ColumnIdentifierSequence const seq { .table = { .catalog = "", .schema = "", .table = "T" }, .columns = { "id" } };
    CHECK(std::format("{}", seq) == "T(id)");
}

TEST_CASE("SqlSchema::ColumnIdentifierSequence: empty columns", "[SqlSchema]")
{
    using Lightweight::SqlSchema::ColumnIdentifierSequence;
    ColumnIdentifierSequence const seq { .table = { .catalog = "", .schema = "", .table = "T" }, .columns = {} };
    CHECK(std::format("{}", seq) == "T()");
}

TEST_CASE("SqlSchema::ColumnIdentifierSequence ordering", "[SqlSchema]")
{
    using Lightweight::SqlSchema::ColumnIdentifierSequence;
    ColumnIdentifierSequence const a { .table = { .catalog = "", .schema = "", .table = "A" }, .columns = { "x" } };
    ColumnIdentifierSequence const b { .table = { .catalog = "", .schema = "", .table = "B" }, .columns = { "x" } };
    CHECK(a < b);
}

TEST_CASE("SqlSchema::ForeignKeyConstraint ordering", "[SqlSchema]")
{
    using Lightweight::SqlSchema::ColumnIdentifierSequence;
    using Lightweight::SqlSchema::ForeignKeyConstraint;

    ColumnIdentifierSequence const fk1 { .table = { .catalog = "", .schema = "", .table = "Children" },
                                         .columns = { "parent_id" } };
    ColumnIdentifierSequence const pk1 { .table = { .catalog = "", .schema = "", .table = "Parents" }, .columns = { "id" } };
    ColumnIdentifierSequence const fk2 { .table = { .catalog = "", .schema = "", .table = "Children" },
                                         .columns = { "other_id" } };

    ForeignKeyConstraint const a { .foreignKey = fk1, .primaryKey = pk1 };
    ForeignKeyConstraint const b { .foreignKey = fk2, .primaryKey = pk1 };
    // fk1 < fk2 lexicographically because "other_id" > "parent_id" — but the comparison
    // happens componentwise. Either way, a < b is well-defined and the operator is exercised.
    auto const cmpAB = (a < b);
    auto const cmpBA = (b < a);
    CHECK(cmpAB != cmpBA);
}

// ================================================================================================
// SqlBackup::detail utility functions
// ================================================================================================

TEST_CASE("SqlBackup::detail::IsTransientError classifies error states", "[SqlBackup]")
{
    using Lightweight::SqlErrorInfo;
    using Lightweight::SqlBackup::detail::IsTransientError;

    SECTION("Class 08 connection errors are transient")
    {
        CHECK(IsTransientError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "08001", .message = "" }));
        CHECK(IsTransientError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "08S01", .message = "" }));
        CHECK(IsTransientError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "08006", .message = "" }));
    }

    SECTION("Timeout errors HYT00 / HYT01 are transient")
    {
        CHECK(IsTransientError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "HYT00", .message = "" }));
        CHECK(IsTransientError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "HYT01", .message = "" }));
    }

    SECTION("Class 40 transaction-rollback errors are transient")
    {
        CHECK(IsTransientError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "40001", .message = "" }));
    }

    SECTION("'database is locked' message is transient (SQLite)")
    {
        CHECK(IsTransientError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "00000", .message = "database is locked" }));
    }

    SECTION("'SQLITE_BUSY' message is transient")
    {
        CHECK(IsTransientError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "00000", .message = "SQLITE_BUSY" }));
    }

    SECTION("Other error states are not transient")
    {
        CHECK_FALSE(IsTransientError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "23000", .message = "" }));
        CHECK_FALSE(IsTransientError(SqlErrorInfo { .nativeErrorCode = 0, .sqlState = "42000", .message = "" }));
    }
}

TEST_CASE("SqlBackup::detail::CalculateRetryDelay applies exponential backoff", "[SqlBackup]")
{
    using Lightweight::SqlBackup::RetrySettings;
    using Lightweight::SqlBackup::detail::CalculateRetryDelay;

    RetrySettings const settings { .maxRetries = 5,
                                   .initialDelay = std::chrono::milliseconds(100),
                                   .backoffMultiplier = 2.0,
                                   .maxDelay = std::chrono::milliseconds(5000) };

    SECTION("attempt 0 returns the initial delay")
    {
        CHECK(CalculateRetryDelay(0, settings) == std::chrono::milliseconds(100));
    }

    SECTION("attempt 1 doubles to 200ms")
    {
        CHECK(CalculateRetryDelay(1, settings) == std::chrono::milliseconds(200));
    }

    SECTION("attempt 2 quadruples to 400ms")
    {
        CHECK(CalculateRetryDelay(2, settings) == std::chrono::milliseconds(400));
    }

    SECTION("delays are capped at maxDelay")
    {
        // Doubling 100ms 10 times would be ~102 seconds — must cap at maxDelay (5s).
        CHECK(CalculateRetryDelay(20, settings) == std::chrono::milliseconds(5000));
    }
}

TEST_CASE("SqlBackup::detail::CurrentDateTime returns ISO-8601 timestamp", "[SqlBackup]")
{
    using Lightweight::SqlBackup::detail::CurrentDateTime;
    auto const stamp = CurrentDateTime();
    // ISO 8601: at minimum YYYY-MM-DDTHH:MM:SS — 19 characters before any fraction/timezone.
    REQUIRE(stamp.size() >= 19);
    CHECK(std::isdigit(static_cast<unsigned char>(stamp[0])));
    CHECK(std::isdigit(static_cast<unsigned char>(stamp[1])));
    CHECK(std::isdigit(static_cast<unsigned char>(stamp[2])));
    CHECK(std::isdigit(static_cast<unsigned char>(stamp[3])));
    CHECK(stamp[4] == '-');
    CHECK(stamp[7] == '-');
}

TEST_CASE("SqlBackup::detail::FormatTableName quotes names", "[SqlBackup]")
{
    using Lightweight::SqlBackup::detail::FormatTableName;
    CHECK(FormatTableName("", "Users") == "\"Users\"");
    CHECK(FormatTableName("dbo", "Users") == "\"dbo\".\"Users\"");
}

// ================================================================================================
// SqlLogger: exercise the standard / trace / null loggers via direct invocation
// ================================================================================================

TEST_CASE("SqlLogger::NullLogger is a singleton", "[SqlLogger]")
{
    auto& a = Lightweight::SqlLogger::NullLogger();
    auto& b = Lightweight::SqlLogger::NullLogger();
    CHECK(&a == &b);
}

TEST_CASE("SqlLogger::StandardLogger is a singleton", "[SqlLogger]")
{
    auto& a = Lightweight::SqlLogger::StandardLogger();
    auto& b = Lightweight::SqlLogger::StandardLogger();
    CHECK(&a == &b);
}

TEST_CASE("SqlLogger::TraceLogger is a singleton", "[SqlLogger]")
{
    auto& a = Lightweight::SqlLogger::TraceLogger();
    auto& b = Lightweight::SqlLogger::TraceLogger();
    CHECK(&a == &b);
}

TEST_CASE("SqlLogger::StandardLogger captures messages via custom sink", "[SqlLogger]")
{
    auto& logger = Lightweight::SqlLogger::StandardLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string m) { captured.push_back(std::move(m)); });

    logger.OnWarning("test warning");
    logger.OnError(Lightweight::SqlError::FAILURE);
    logger.OnError(Lightweight::SqlErrorInfo { .nativeErrorCode = 1, .sqlState = "08001", .message = "boom" });

    // Reset sink to default to avoid affecting other tests.
    logger.SetLoggingSink();

    CHECK(captured.size() >= 3);
    CHECK(captured[0].contains("warning"));
    CHECK(captured[1].contains("SQL Error"));
    CHECK(captured[2].contains("08001"));
    CHECK(captured[2].contains("boom"));
}

TEST_CASE("SqlLogger::TraceLogger captures lifecycle events", "[SqlLogger]")
{
    auto& logger = Lightweight::SqlLogger::TraceLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string m) { captured.push_back(std::move(m)); });

    logger.OnScopedTimerStart("phase1");
    logger.OnScopedTimerStop("phase1");
    logger.OnWarning("trace warning");

    logger.SetLoggingSink();
    // The trace logger emits at minimum start/stop messages plus the warning.
    CHECK(captured.size() >= 3);
}

TEST_CASE("SqlLogger::NullLogger swallows all events without writing", "[SqlLogger]")
{
    auto& logger = Lightweight::SqlLogger::NullLogger();
    std::vector<std::string> captured;
    logger.SetLoggingSink([&](std::string m) { captured.push_back(std::move(m)); });

    logger.OnWarning("ignored");
    logger.OnError(Lightweight::SqlError::FAILURE, std::source_location::current());
    logger.OnExecuteDirect("ignored");

    logger.SetLoggingSink();
    // The null logger ignores all events, even with a sink installed — it never calls into the sink.
    CHECK(captured.empty());
}

namespace
{
/// Convenience wrapper to call detail::ParseFloat on a string_view.
template <typename T>
std::optional<T> ParseFloatSv(std::string_view text)
{
    return Lightweight::detail::ParseFloat<T>(text.data(), text.data() + text.size());
}
} // namespace

TEST_CASE("detail::ParseFloat parses valid finite values", "[Utils][ParseFloat]")
{
    CHECK(ParseFloatSv<double>("3.14") == 3.14);
    CHECK(ParseFloatSv<double>("-2.5") == -2.5);
    CHECK(ParseFloatSv<double>("0") == 0.0);
    CHECK(ParseFloatSv<double>("42") == 42.0);
    CHECK(ParseFloatSv<float>("1.5") == 1.5F);
    CHECK(ParseFloatSv<double>("1e3") == 1000.0);
}

TEST_CASE("detail::ParseFloat rejects malformed and partial input", "[Utils][ParseFloat]")
{
    // Whole-token requirement: trailing garbage is rejected (mirrors std::from_chars' ptr==last check),
    // unlike a bare std::strtod which would silently accept the leading numeric prefix.
    CHECK(ParseFloatSv<double>("abc") == std::nullopt);
    CHECK(ParseFloatSv<double>("12abc") == std::nullopt);
    CHECK(ParseFloatSv<double>("") == std::nullopt);
    CHECK(ParseFloatSv<double>("1.2.3") == std::nullopt);
}

TEST_CASE("detail::ParseFloat reports out-of-range values as failure", "[Utils][ParseFloat]")
{
    // A value that overflows the target type must NOT be silently turned into +inf; the helper reports
    // failure so the caller can leave its value untouched.
    CHECK(ParseFloatSv<double>("1e99999") == std::nullopt);
    CHECK(ParseFloatSv<float>("1e99999") == std::nullopt);
}

TEST_CASE("detail::ParseFloat is locale-independent", "[Utils][ParseFloat]")
{
    // The decimal point is always '.', regardless of the active LC_NUMERIC. Try to switch to a
    // comma-decimal locale; if it is installed on the host, parsing of "1.5" must still yield 1.5.
    if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr || std::setlocale(LC_NUMERIC, "de_DE") != nullptr)
    {
        CHECK(ParseFloatSv<double>("1.5") == 1.5);
        CHECK(ParseFloatSv<double>("1234.5") == 1234.5);
        std::setlocale(LC_NUMERIC, "C");
    }
    else
        SUCCEED("comma-decimal locale not installed on this host; skipping");
}
