// SPDX-License-Identifier: Apache-2.0

// Backup and restore of archives that span several schemas, with foreign keys between them.

#include "../../tools/dbtool/BackupDiff.hpp"
#include "../Utils.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlBackup/Common.hpp>
#include <Lightweight/SqlBackup/MsgPackChunkFormats.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlQuery/MigrationPlan.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Zip/ZipArchive.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using namespace Lightweight;
using Lightweight::SqlBackup::Tests::MakeChunk;
using Lightweight::SqlBackup::Tests::ScopedFileRemoved;

namespace
{

/// What @ref ErrorRecordingProgressManager::Errors() returns when nothing failed.
constexpr auto NoErrors = std::string_view { "(no errors)" };

/// Records the error messages a backup or restore reports.
struct ErrorRecordingProgressManager: SqlBackup::ErrorTrackingProgressManager
{
    std::mutex mutex;
    std::vector<std::string> errors;

    void Update(SqlBackup::Progress const& progress) override
    {
        auto const lock = std::scoped_lock(mutex);
        ErrorTrackingProgressManager::Update(progress);
        if (progress.state == SqlBackup::Progress::State::Error)
            errors.push_back(std::format("{}: {}", progress.tableName, progress.message));
    }

    void AllDone() override {}

    /// Returns all error messages, one per line, or @ref NoErrors.
    [[nodiscard]] std::string Errors()
    {
        auto const lock = std::scoped_lock(mutex);
        std::string text;
        for (auto const& error: errors)
            text += error + "\n";
        return text.empty() ? std::string { NoErrors } : text;
    }
};

/// Reads the entry @p entryName of the ZIP archive @p archive.
std::string ReadArchiveEntry(std::filesystem::path const& archive, std::string const& entryName)
{
    auto const zip = Zip::ZipArchive::Open(archive);
    REQUIRE(zip.has_value());
    auto const index = zip->LocateEntry(entryName).value_or(-1);
    REQUIRE(index >= 0);
    auto content = zip->ReadEntryAsString(index);
    REQUIRE(content.has_value());
    return std::move(*content);
}

/// Lists the entry names of the ZIP archive @p archive.
std::vector<std::string> ArchiveEntryNames(std::filesystem::path const& archive)
{
    auto const zip = Zip::ZipArchive::Open(archive);
    REQUIRE(zip.has_value());
    std::vector<std::string> names;
    for (auto const& entry: zip->GetAllEntries())
        names.push_back(entry.name);
    return names;
}

/// Writes a ZIP archive holding @p entries (entry name -> content).
void WriteArchive(std::filesystem::path const& archive, std::map<std::string, std::string> const& entries)
{
    auto zip = Zip::ZipArchive::CreateOrTruncate(archive);
    REQUIRE(zip.has_value());
    for (auto const& [name, content]: entries)
        REQUIRE(zip->AddString(name, content).has_value());
    REQUIRE(zip->Close().has_value());
}

/// Returns the metadata JSON of a table with an integer primary key `id` and a second column.
nlohmann::json TableJson(
    std::string_view schema, std::string_view name, std::string_view secondColumn, std::string_view secondType, size_t rows)
{
    auto table = nlohmann::json {
        { "name", name },
        { "rows", rows },
        { "columns",
          nlohmann::json::array({
              { { "name", "id" },
                { "type", "integer" },
                { "is_primary_key", true },
                { "is_auto_increment", false },
                { "is_nullable", false },
                { "is_unique", false } },
              { { "name", secondColumn },
                { "type", secondType },
                { "size", 50 },
                { "is_primary_key", false },
                { "is_auto_increment", false },
                { "is_nullable", true },
                { "is_unique", false } },
          }) },
        { "foreign_keys", nlohmann::json::array() },
        { "indexes", nlohmann::json::array() },
        { "primary_keys", nlohmann::json::array({ "id" }) },
    };
    if (!schema.empty())
        table["schema"] = schema;
    return table;
}

constexpr auto CoreSchema = std::string_view { "lw_l7_core" };
constexpr auto ExtraSchema = std::string_view { "lw_l7_extra" };

/// Returns `"schema"."table"`, or `"table"` where the database has no schemas.
std::string QualifiedName(SqlStatement const& stmt, std::string_view schema, std::string_view table)
{
    if (!stmt.Connection().QueryFormatter().SupportsSchemas())
        return std::format(R"("{}")", table);
    return std::format(R"("{}"."{}")", schema, table);
}

/// Executes @p sql for best-effort cleanup.
/// @return `false` if it failed, typically because the object did not exist.
bool ExecuteIgnoringErrors(SqlStatement& stmt, std::string const& sql)
{
    try
    {
        (void) stmt.ExecuteDirect(sql);
        return true;
    }
    catch (SqlException const&)
    {
        return false;
    }
}

/// Drops the tables @p tables (children first) and then the schemas @p schemas.
void DropSchemaFixture(std::vector<std::pair<std::string_view, std::string_view>> const& tables,
                       std::vector<std::string_view> const& schemas)
{
    auto stmt = SqlStatement {};
    for (auto const& [schema, table]: tables)
        (void) ExecuteIgnoringErrors(stmt, std::format("DROP TABLE IF EXISTS {}", QualifiedName(stmt, schema, table)));
    if (!stmt.Connection().QueryFormatter().SupportsSchemas())
        return;
    for (auto const schema: schemas)
        (void) ExecuteIgnoringErrors(stmt, std::format(R"(DROP SCHEMA IF EXISTS "{}")", schema));
}

/// Drops the tables and schemas of the two-schema fixture.
void DropCoreExtraFixture()
{
    DropSchemaFixture(
        { { ExtraSchema, "child" }, { ExtraSchema, "shared" }, { CoreSchema, "shared" }, { CoreSchema, "parent" } },
        { ExtraSchema, CoreSchema });
}

/// Creates `lw_l7_core.parent`, `lw_l7_extra.child` (with a foreign key to the parent) and a
/// table `shared` in both schemas, each holding different rows.
void CreateCoreExtraFixture()
{
    DropCoreExtraFixture();
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect(std::format(R"(CREATE SCHEMA "{}")", CoreSchema));
    (void) stmt.ExecuteDirect(std::format(R"(CREATE SCHEMA "{}")", ExtraSchema));
    (void) stmt.ExecuteDirect(
        std::format(R"(CREATE TABLE "{}"."parent" ("id" INTEGER NOT NULL PRIMARY KEY, "name" VARCHAR(50)))", CoreSchema));
    (void) stmt.ExecuteDirect(
        std::format(R"(CREATE TABLE "{}"."child" ("id" INTEGER NOT NULL PRIMARY KEY, "parent_id" INTEGER,
                       CONSTRAINT "fk_l7_child_parent" FOREIGN KEY ("parent_id") REFERENCES "{}"."parent" ("id")))",
                    ExtraSchema,
                    CoreSchema));
    for (auto const schema: { CoreSchema, ExtraSchema })
        (void) stmt.ExecuteDirect(
            std::format(R"(CREATE TABLE "{}"."shared" ("id" INTEGER NOT NULL PRIMARY KEY, "origin" VARCHAR(50)))", schema));

    (void) stmt.ExecuteDirect(
        std::format(R"(INSERT INTO "{}"."parent" ("id", "name") VALUES (1, 'first'), (2, 'second'))", CoreSchema));
    (void) stmt.ExecuteDirect(
        std::format(R"(INSERT INTO "{}"."child" ("id", "parent_id") VALUES (10, 1), (11, 2), (12, 2))", ExtraSchema));
    (void) stmt.ExecuteDirect(std::format(R"(INSERT INTO "{}"."shared" ("id", "origin") VALUES (1, 'core'))", CoreSchema));
    (void) stmt.ExecuteDirect(
        std::format(R"(INSERT INTO "{}"."shared" ("id", "origin") VALUES (1, 'extra'), (2, 'extra'))", ExtraSchema));
}

/// Returns the row count of @p qualifiedTable, or -1 if it cannot be read.
int64_t CountRows(std::string const& qualifiedTable)
{
    try
    {
        return SqlStatement {}
            .ExecuteDirectScalar<int64_t>(std::format("SELECT COUNT(*) FROM {}", qualifiedTable))
            .value_or(-1);
    }
    catch (SqlException const&)
    {
        return -1;
    }
}

/// Tests whether the foreign key of `lw_l7_extra.child` rejects a row whose parent does not exist.
/// A missing child table fails the test rather than counting as a rejection.
bool ChildRejectsOrphan()
{
    REQUIRE(CountRows(std::format(R"("{}"."child")", ExtraSchema)) >= 0);
    try
    {
        (void) SqlStatement {}.ExecuteDirect(
            std::format(R"(INSERT INTO "{}"."child" ("id", "parent_id") VALUES (99, 42))", ExtraSchema));
        return false;
    }
    catch (SqlException const&)
    {
        return true;
    }
}

/// Deletes the fixture's tables and schemas when it goes out of scope.
struct ScopedCoreExtraFixture
{
    ScopedCoreExtraFixture()
    {
        CreateCoreExtraFixture();
    }
    ~ScopedCoreExtraFixture()
    {
        DropCoreExtraFixture();
    }
    ScopedCoreExtraFixture(ScopedCoreExtraFixture const&) = delete;
    ScopedCoreExtraFixture& operator=(ScopedCoreExtraFixture const&) = delete;
    ScopedCoreExtraFixture(ScopedCoreExtraFixture&&) = delete;
    ScopedCoreExtraFixture& operator=(ScopedCoreExtraFixture&&) = delete;
};

} // namespace

// ================================================================================================
// Without a database
// ================================================================================================

TEST_CASE("SqlBackup: chunk folders of single- and multi-schema archives", "[SqlBackup][MultiSchema]")
{
    using SqlBackup::detail::ChunkDirectory;
    using SqlBackup::detail::ChunkDirectoryOf;

    CHECK(ChunkDirectory("", "Users") == "Users");
    CHECK(ChunkDirectory("", "odd.name") == "odd_name");
    CHECK(ChunkDirectory("sales", "CUSTOMERS") == "sales/CUSTOMERS");
    CHECK(ChunkDirectory("my.schema", "odd.name") == "my_schema/odd_name");

    CHECK(ChunkDirectoryOf("data/Users/0001_00.msgpack") == "Users");
    CHECK(ChunkDirectoryOf("data/sales/CUSTOMERS/0001_00.msgpack") == "sales/CUSTOMERS");
    CHECK_FALSE(ChunkDirectoryOf("metadata.json").has_value());
    CHECK_FALSE(ChunkDirectoryOf("data/0001.msgpack").has_value());
    CHECK_FALSE(ChunkDirectoryOf("data/Users/readme.txt").has_value());
}

TEST_CASE("ParseSchema: a multi-schema archive keys tables by schema and records referenced schemas",
          "[SqlBackup][ParseSchema][MultiSchema]")
{
    auto metadata = nlohmann::json {
        { "format_version", "2.0" },
        { "schema_name", "" },
        { "schema_names", { "dbo", "sales" } },
        { "schema", nlohmann::json::array() },
    };
    auto orders = TableJson("dbo", "ORDERS", "CUSTOMER_ID", "integer", 1);
    orders["foreign_keys"].push_back({ { "name", "" },
                                       { "columns", { "CUSTOMER_ID" } },
                                       { "referenced_table", "CUSTOMERS" },
                                       { "referenced_columns", { "id" } },
                                       { "referenced_schema", "sales" } });
    metadata["schema"].push_back(orders);
    metadata["schema"].push_back(TableJson("sales", "CUSTOMERS", "label", "varchar", 2));
    metadata["schema"].push_back(TableJson("sales", "ORDERS", "label", "varchar", 0));

    auto const tables = SqlBackup::ParseSchema(metadata.dump());

    REQUIRE(tables.size() == 3);
    REQUIRE(tables.contains("dbo.ORDERS"));
    REQUIRE(tables.contains("sales.ORDERS"));
    REQUIRE(tables.contains("sales.CUSTOMERS"));
    auto const& ordersInfo = tables.at("dbo.ORDERS");
    CHECK(ordersInfo.name == "ORDERS");
    CHECK(ordersInfo.schema == "dbo");
    REQUIRE(ordersInfo.foreignKeys.size() == 1);
    CHECK(ordersInfo.foreignKeys[0].foreignKey.table.schema == "dbo");
    CHECK(ordersInfo.foreignKeys[0].primaryKey.table.schema == "sales");
    CHECK(ordersInfo.foreignKeys[0].primaryKey.table.table == "CUSTOMERS");
    CHECK(tables.at("sales.CUSTOMERS").rowCount == 2);
}

TEST_CASE("ParseSchema: a single-schema archive keys tables by name and records no schemas",
          "[SqlBackup][ParseSchema][MultiSchema]")
{
    auto metadata = nlohmann::json {
        { "format_version", "1.0" },
        { "schema_name", "dbo" },
        { "schema", nlohmann::json::array({ TableJson("", "Users", "login", "varchar", 0) }) },
    };
    metadata["schema"][0]["foreign_keys"].push_back({ { "name", "" },
                                                      { "columns", { "login" } },
                                                      { "referenced_table", "Logins" },
                                                      { "referenced_columns", { "id" } } });

    auto const tables = SqlBackup::ParseSchema(metadata.dump());

    REQUIRE(tables.size() == 1);
    auto const& users = tables.at("Users");
    CHECK(users.name == "Users");
    CHECK(users.schema.empty());
    REQUIRE(users.foreignKeys.size() == 1);
    CHECK(users.foreignKeys[0].primaryKey.table.schema.empty());
}

TEST_CASE("SqlQueryFormatter: schema support and CREATE SCHEMA per dialect", "[SqlBackup][MultiSchema]")
{
    CHECK_FALSE(SqlQueryFormatter::Sqlite().SupportsSchemas());
    CHECK(SqlQueryFormatter::Sqlite().CreateSchemaIfNotExists("sales").empty());

    CHECK(SqlQueryFormatter::PostgrSQL().SupportsSchemas());
    // The existence test comes first: CREATE SCHEMA IF NOT EXISTS needs the database's CREATE privilege.
    CHECK(SqlQueryFormatter::PostgrSQL().CreateSchemaIfNotExists("sales")
          == R"(DO $lw$ BEGIN IF NOT EXISTS (SELECT 1 FROM pg_namespace WHERE nspname = 'sales') THEN )"
             R"(CREATE SCHEMA "sales"; END IF; END $lw$)");
    CHECK(SqlQueryFormatter::PostgrSQL().CreateSchemaIfNotExists(R"(a"b'c)")
          == R"(DO $lw$ BEGIN IF NOT EXISTS (SELECT 1 FROM pg_namespace WHERE nspname = 'a"b''c') THEN )"
             R"(CREATE SCHEMA "a""b'c"; END IF; END $lw$)");
    // The dollar-quote tag never occurs in the name, which would otherwise end the DO body.
    CHECK(SqlQueryFormatter::PostgrSQL().CreateSchemaIfNotExists("x$lw$y")
          == R"(DO $lw1$ BEGIN IF NOT EXISTS (SELECT 1 FROM pg_namespace WHERE nspname = 'x$lw$y') THEN )"
             R"(CREATE SCHEMA "x$lw$y"; END IF; END $lw1$)");

    CHECK(SqlQueryFormatter::SqlServer().SupportsSchemas());
    CHECK(SqlQueryFormatter::SqlServer().CreateSchemaIfNotExists("sales")
          == R"(IF SCHEMA_ID(N'sales') IS NULL EXEC(N'CREATE SCHEMA "sales"'))");
    CHECK(SqlQueryFormatter::SqlServer().CreateSchemaIfNotExists("o'k")
          == R"(IF SCHEMA_ID(N'o''k') IS NULL EXEC(N'CREATE SCHEMA "o''k"'))");
}

TEST_CASE("SqlQueryFormatter: an INSERT into a schema names the schema where the dialect has schemas",
          "[SqlBackup][MultiSchema]")
{
    CHECK(SqlQueryFormatter::Sqlite().Insert("sales", "CUSTOMERS", R"("id")", "?")
          == R"(INSERT INTO "CUSTOMERS" ("id") VALUES (?))");
    for (auto const* formatter: { &SqlQueryFormatter::SqlServer(), &SqlQueryFormatter::PostgrSQL() })
    {
        CHECK(formatter->Insert("sales", "CUSTOMERS", R"("id")", "?")
              == R"(INSERT INTO "sales"."CUSTOMERS" ("id") VALUES (?))");
        CHECK(formatter->Insert("", "CUSTOMERS", R"("id")", "?") == R"(INSERT INTO "CUSTOMERS" ("id") VALUES (?))");
        CHECK(formatter->Insert(R"(a"b)", "CUSTOMERS", R"("id")", "?")
              == R"(INSERT INTO "a""b"."CUSTOMERS" ("id") VALUES (?))");
    }
}

TEST_CASE("SqlQueryFormatter: CREATE TABLE names the schema only where the dialect has schemas", "[SqlBackup][MultiSchema]")
{
    auto const columns = std::vector<SqlColumnDeclaration> {
        { .name = "id", .type = SqlColumnTypeDefinitions::Integer {}, .primaryKey = SqlPrimaryKeyType::MANUAL },
        { .name = "label", .type = SqlColumnTypeDefinitions::Varchar { .size = 20 }, .index = true },
    };
    for (auto const& sql: SqlQueryFormatter::Sqlite().CreateTable("sales", "CUSTOMERS", columns, {}))
        CHECK_FALSE(sql.contains("sales"));

    auto const postgreSql = SqlQueryFormatter::PostgrSQL().CreateTable("sales", "CUSTOMERS", columns, {});
    REQUIRE(postgreSql.size() == 2);
    CHECK(postgreSql[0].starts_with(R"(CREATE TABLE "sales"."CUSTOMERS" ()"));
    CHECK(postgreSql[1].contains(R"(ON "sales"."CUSTOMERS")"));
}

TEST_CASE("SqlBackup: a profile's schema is dropped for a multi-schema archive only when the archive holds it",
          "[SqlBackup][MultiSchema]")
{
    std::filesystem::path const multiSchemaFile = "l7_default_schema_multi.zip";
    std::filesystem::path const singleSchemaFile = "l7_default_schema_single.zip";
    auto const multiSchemaCleaner = ScopedFileRemoved { multiSchemaFile };
    auto const singleSchemaCleaner = ScopedFileRemoved { singleSchemaFile };
    WriteArchive(multiSchemaFile,
                 { { "metadata.json",
                     nlohmann::json { { "format_version", "2.0" },
                                      { "schema_name", "" },
                                      { "schema_names", { "dbo", "sales" } },
                                      { "schema", nlohmann::json::array() } }
                         .dump() } });
    WriteArchive(
        singleSchemaFile,
        { { "metadata.json",
            nlohmann::json { { "format_version", "1.0" }, { "schema_name", "dbo" }, { "schema", nlohmann::json::array() } }
                .dump() } });

    for (bool const caseInsensitive: { true, false })
    {
        CHECK(SqlBackup::DefaultRestoreSchema(multiSchemaFile, "sales", caseInsensitive).empty());
        CHECK(SqlBackup::DefaultRestoreSchema(multiSchemaFile, "restore_check", caseInsensitive) == "restore_check");
        CHECK(SqlBackup::DefaultRestoreSchema(multiSchemaFile, "", caseInsensitive).empty());
        CHECK(SqlBackup::DefaultRestoreSchema(singleSchemaFile, "elsewhere", caseInsensitive) == "elsewhere");
        CHECK(SqlBackup::DefaultRestoreSchema("l7_no_such_archive.zip", "dbo", caseInsensitive) == "dbo");
    }
    // The case of a name matters only where the target compares identifiers case-sensitively (PostgreSQL).
    CHECK(SqlBackup::DefaultRestoreSchema(multiSchemaFile, "DBO", true).empty());
    CHECK(SqlBackup::DefaultRestoreSchema(multiSchemaFile, "DBO", false) == "DBO");
}

TEST_CASE("SqlBackup: a multi-schema restore stops when it cannot reach the target", "[SqlBackup][MultiSchema]")
{
    std::filesystem::path const backupFile = "l7_unreachable_target.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    WriteArchive(backupFile,
                 { { "metadata.json",
                     nlohmann::json {
                         { "format_version", "2.0" },
                         { "schema_name", "" },
                         { "schema_names", { "dbo", "sales" } },
                         { "schema", nlohmann::json::array({ TableJson("sales", "CUSTOMERS", "label", "varchar", 0) }) } }
                         .dump() } });

    ErrorRecordingProgressManager progress;
    SqlBackup::Restore(backupFile, SqlConnectionString { "DRIVER={Lightweight No Such Driver}" }, 1, progress);

    CHECK(progress.Errors().contains("Failed to connect to the target database"));
}

TEST_CASE("BackupDiff: --ignore-table matches a bare table name only when one schema holds it", "[SqlBackup][MultiSchema]")
{
    std::filesystem::path const leftFile = "l7_diff_left.zip";
    std::filesystem::path const rightFile = "l7_diff_right.zip";
    auto const leftCleaner = ScopedFileRemoved { leftFile };
    auto const rightCleaner = ScopedFileRemoved { rightFile };
    auto const metadata = nlohmann::json {
        { "format_version", "2.0" },
        { "schema_name", "" },
        { "schema_names", { "dbo", "sales" } },
        { "schema", nlohmann::json::array() }
    }.dump();
    auto const row = [](int64_t id) {
        return MakeChunk({ { id, std::string { "x" } } });
    };
    WriteArchive(leftFile,
                 { { "metadata.json", metadata },
                   { "data/dbo/AUDIT_LOG/0001_00.msgpack", row(1) },
                   { "data/dbo/shared/0001_00.msgpack", row(1) },
                   { "data/sales/shared/0001_00.msgpack", row(1) } });
    WriteArchive(rightFile,
                 { { "metadata.json", metadata },
                   { "data/dbo/AUDIT_LOG/0001_00.msgpack", row(2) },
                   { "data/dbo/shared/0001_00.msgpack", row(2) },
                   { "data/sales/shared/0001_00.msgpack", row(1) } });

    auto const unique = Tools::BackupDiff(leftFile, rightFile, { "AUDIT_LOG", "dbo/shared" }, nullptr);
    CHECK(unique.ignoreTableError.empty());
    CHECK_FALSE(unique.differenceFound);
    CHECK(unique.ignoredDifferences == 2);
    CHECK(unique.unmatchedIgnoreTables.empty());

    auto const dotted = Tools::BackupDiff(leftFile, rightFile, { "dbo.AUDIT_LOG", "dbo.shared", "NO_SUCH_TABLE" }, nullptr);
    CHECK(dotted.ignoreTableError.empty());
    CHECK_FALSE(dotted.differenceFound);
    CHECK(dotted.unmatchedIgnoreTables == std::vector<std::string> { "NO_SUCH_TABLE" });

    auto const ambiguous = Tools::BackupDiff(leftFile, rightFile, { "shared" }, nullptr);
    CHECK(ambiguous.ignoreTableError.contains("ambiguous"));
    CHECK(ambiguous.ignoreTableError.contains("dbo/shared"));
    CHECK(ambiguous.ignoreTableError.contains("sales/shared"));
    CHECK(ambiguous.differenceFound);
}

TEST_CASE("SqlQueryFormatter: a composite foreign key may reference another schema", "[SqlBackup][MultiSchema]")
{
    auto const commands = std::vector<SqlAlterTableCommand> {
        SqlAlterTableCommands::AddCompositeForeignKey {
            .columns = { "CUSTOMER_ID" },
            .referencedTableName = "CUSTOMERS",
            .referencedColumns = { "id" },
            .referencedSchemaName = "sales",
        },
    };
    for (auto const* formatter: { &SqlQueryFormatter::SqlServer(), &SqlQueryFormatter::PostgrSQL() })
    {
        auto const sqls = formatter->AlterTable("dbo", "ORDERS", commands);
        REQUIRE(sqls.size() == 1);
        CHECK(sqls[0].contains(R"(ALTER TABLE "dbo"."ORDERS")"));
        CHECK(sqls[0].contains(R"(REFERENCES "sales"."CUSTOMERS" ("id"))"));
    }
}

TEST_CASE("SqlBackup: the collision message advises a multi-schema archive only for a single-schema backup",
          "[SqlBackup][MultiSchema]")
{
    auto const makeTable = [](std::string schema, std::string name) {
        auto table = SqlSchema::Table {};
        table.schema = std::move(schema);
        table.name = std::move(name);
        return table;
    };
    auto const sameNameInTwoSchemas = SqlSchema::TableList { makeTable("dbo", "ORDERS"), makeTable("sales", "ORDERS") };
    CHECK(SqlBackup::detail::DescribeTableNameCollisions(sameNameInTwoSchemas, false).contains("--all-schemas"));
    CHECK(SqlBackup::detail::DescribeTableNameCollisions(sameNameInTwoSchemas, true).empty());

    // "a.b" and "a_b" share the chunk folder "a_b/ORDERS".
    auto const folderClash = SqlSchema::TableList { makeTable("a.b", "ORDERS"), makeTable("a_b", "ORDERS") };
    auto const message = SqlBackup::detail::DescribeTableNameCollisions(folderClash, true);
    CHECK(message.contains("share an archive folder"));
    CHECK_FALSE(message.contains("--all-schemas"));
}

TEST_CASE("SqlBackup: the schemas of a backup count each name once", "[SqlBackup][MultiSchema]")
{
    using SqlBackup::detail::NormalizeSchemaNames;
    CHECK(NormalizeSchemaNames({ "sales", "dbo", "dbo" }, false) == std::vector<std::string> { "dbo", "sales" });
    CHECK(NormalizeSchemaNames({ "dbo", "DBO" }, true) == std::vector<std::string> { "dbo" });
    CHECK(NormalizeSchemaNames({ "dbo", "DBO" }, false) == std::vector<std::string> { "DBO", "dbo" });
    CHECK(NormalizeSchemaNames({}, false) == std::vector<std::string> { "" });
}

// ================================================================================================
// Against the test database
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: a single schema still writes the version 1.0 archive layout",
                 "[SqlBackup][MultiSchema]")
{
    std::filesystem::path const backupFile = "l7_single_schema.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };

    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "l7_single" ("id" INTEGER NOT NULL PRIMARY KEY, "label" VARCHAR(50)))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "l7_single" ("id", "label") VALUES (1, 'one'))");

    ErrorRecordingProgressManager progress;
    SqlBackup::BackupSchemas(backupFile, SqlConnection::DefaultConnectionString(), 1, progress, {}, "l7_single");
    REQUIRE(progress.Errors() == NoErrors);

    auto const metadata = nlohmann::json::parse(ReadArchiveEntry(backupFile, "metadata.json"));
    CHECK(metadata["format_version"] == "1.0");
    CHECK_FALSE(metadata.contains("schema_names"));
    REQUIRE(metadata["schema"].size() == 1);
    CHECK_FALSE(metadata["schema"][0].contains("schema"));
    auto const entries = ArchiveEntryNames(backupFile);
    CHECK(std::ranges::any_of(entries, [](std::string const& name) { return name.starts_with("data/l7_single/"); }));
    auto const archiveSchemas = SqlBackup::ReadArchiveSchemas(backupFile);
    REQUIRE(archiveSchemas.has_value());
    CHECK(*archiveSchemas == std::vector<std::string> { "" });
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: one named schema writes the archive Backup writes and restores into it",
                 "[SqlBackup][MultiSchema]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE); // SQLite has no named schemas

    std::filesystem::path const backupFile = "l7_named_single.zip";
    std::filesystem::path const listedFile = "l7_named_single_listed.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const listedFileCleaner = ScopedFileRemoved { listedFile };
    auto const fixture = ScopedCoreExtraFixture {};
    auto const& connectionString = SqlConnection::DefaultConnectionString();
    auto const core = std::string { CoreSchema };
    // A case-insensitive dialect treats differently cased names as one schema.
    auto sameSchema = core;
    if (stmt.Connection().QueryFormatter().IdentifiersAreCaseInsensitive())
        for (auto& character: sameSchema)
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

    ErrorRecordingProgressManager progress;
    SqlBackup::Backup(backupFile, connectionString, 1, progress, core);
    SqlBackup::BackupSchemas(listedFile, connectionString, 1, progress, { core, sameSchema, core });
    REQUIRE(progress.Errors() == NoErrors);

    auto metadata = nlohmann::json::parse(ReadArchiveEntry(backupFile, "metadata.json"));
    auto listedMetadata = nlohmann::json::parse(ReadArchiveEntry(listedFile, "metadata.json"));
    CHECK(metadata["format_version"] == "1.0");
    CHECK(metadata["schema_name"] == core);
    metadata.erase("creation_time");
    listedMetadata.erase("creation_time");
    CHECK(metadata == listedMetadata);
    auto entries = ArchiveEntryNames(backupFile);
    auto listedEntries = ArchiveEntryNames(listedFile);
    std::ranges::sort(entries);
    std::ranges::sort(listedEntries);
    CHECK(entries == listedEntries);
    CHECK(std::ranges::any_of(entries, [](std::string const& name) { return name.starts_with("data/parent/"); }));

    DropSchemaFixture({ { ExtraSchema, "child" }, { CoreSchema, "shared" }, { CoreSchema, "parent" } }, {});
    ErrorRecordingProgressManager restoreProgress;
    SqlBackup::Restore(backupFile, connectionString, 1, restoreProgress);
    CHECK(restoreProgress.Errors() == NoErrors);
    CHECK(CountRows(R"("lw_l7_core"."parent")") == 2);
    CHECK(CountRows(R"("lw_l7_core"."shared")") == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: a backup refuses tables whose archive folders collide",
                 "[SqlBackup][MultiSchema]")
{
    std::filesystem::path const backupFile = "l7_colliding_folders.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "l7.dot" ("id" INTEGER NOT NULL PRIMARY KEY))");
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "l7_dot" ("id" INTEGER NOT NULL PRIMARY KEY))");

    ErrorRecordingProgressManager progress;
    SqlBackup::Backup(backupFile, SqlConnection::DefaultConnectionString(), 1, progress, {}, "l7?dot");

    CHECK(progress.Errors().contains("would share an archive folder"));
    CHECK(progress.Errors().contains("l7_dot"));
    (void) ExecuteIgnoringErrors(stmt, R"(DROP TABLE "l7.dot")");
    (void) ExecuteIgnoringErrors(stmt, R"(DROP TABLE "l7_dot")");
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: a backup of no named schema refuses same-named tables of two schemas",
                 "[SqlBackup][MultiSchema]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE); // SQLite has no schemas

    std::filesystem::path const backupFile = "l7_same_name_two_schemas.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const fixture = ScopedCoreExtraFixture {};

    ErrorRecordingProgressManager progress;
    SqlBackup::Backup(backupFile, SqlConnection::DefaultConnectionString(), 1, progress, {}, "shared");

    auto const errors = progress.Errors();
    CHECK(errors.contains("tables of the same name exist in several schemas"));
    CHECK(errors.contains("lw_l7_core.shared, lw_l7_extra.shared"));
    CHECK(errors.contains("--all-schemas"));
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: an archive in the version 1.0 layout still restores",
                 "[SqlBackup][MultiSchema]")
{
    std::filesystem::path const backupFile = "l7_legacy_layout.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };

    // Chunk names of older releases carry no window suffix.
    auto metadata = nlohmann::json {
        { "format_version", "1.0" },
        { "schema_name", "" },
        { "schema", nlohmann::json::array({ TableJson("", "l7_legacy", "label", "varchar", 2) }) },
    };
    WriteArchive(backupFile,
                 {
                     { "metadata.json", metadata.dump() },
                     { "data/l7_legacy/0001.msgpack",
                       MakeChunk({ { int64_t { 1 }, std::string { "one" } }, { int64_t { 2 }, std::string { "two" } } }) },
                 });

    ErrorRecordingProgressManager progress;
    SqlBackup::Restore(backupFile, SqlConnection::DefaultConnectionString(), 1, progress);

    CHECK(progress.Errors() == NoErrors);
    CHECK(CountRows(R"("l7_legacy")") == 2);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: tables of two schemas round-trip with a cross-schema foreign key",
                 "[SqlBackup][MultiSchema]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE); // SQLite has no schemas to back up

    std::filesystem::path const backupFile = "l7_two_schemas.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const fixture = ScopedCoreExtraFixture {};
    auto const& connectionString = SqlConnection::DefaultConnectionString();

    ErrorRecordingProgressManager backupProgress;
    SqlBackup::BackupSchemas(
        backupFile, connectionString, 2, backupProgress, { std::string { ExtraSchema }, std::string { CoreSchema } });
    REQUIRE(backupProgress.Errors() == NoErrors);

    auto const metadata = nlohmann::json::parse(ReadArchiveEntry(backupFile, "metadata.json"));
    CHECK(metadata["format_version"] == "2.0");
    CHECK(metadata["schema_names"] == nlohmann::json::array({ CoreSchema, ExtraSchema }));
    auto const entries = ArchiveEntryNames(backupFile);
    for (auto const* folder:
         { "data/lw_l7_core/parent/", "data/lw_l7_extra/child/", "data/lw_l7_core/shared/", "data/lw_l7_extra/shared/" })
        CHECK(std::ranges::any_of(entries, [folder](std::string const& name) { return name.starts_with(folder); }));
    auto const archiveSchemas = SqlBackup::ReadArchiveSchemas(backupFile);
    REQUIRE(archiveSchemas.has_value());
    CHECK(*archiveSchemas == std::vector<std::string> { std::string { CoreSchema }, std::string { ExtraSchema } });

    SECTION("restore recreates the schemas, the data and the foreign key")
    {
        DropCoreExtraFixture();

        ErrorRecordingProgressManager restoreProgress;
        SqlBackup::Restore(backupFile, connectionString, 2, restoreProgress);
        CHECK(restoreProgress.Errors() == NoErrors);

        CHECK(CountRows(R"("lw_l7_core"."parent")") == 2);
        CHECK(CountRows(R"("lw_l7_extra"."child")") == 3);
        CHECK(CountRows(R"("lw_l7_core"."shared")") == 1);
        CHECK(CountRows(R"("lw_l7_extra"."shared")") == 2);
        CHECK(SqlStatement {}
                  .ExecuteDirectScalar<std::string>(R"(SELECT "origin" FROM "lw_l7_core"."shared" WHERE "id" = 1)")
                  .value_or("")
              == "core");
        CHECK(ChildRejectsOrphan());
    }

    SECTION("a schema-only restore recreates empty tables and the foreign key")
    {
        DropCoreExtraFixture();

        ErrorRecordingProgressManager restoreProgress;
        SqlBackup::Restore(backupFile,
                           connectionString,
                           1,
                           restoreProgress,
                           {},
                           "*",
                           {},
                           SqlBackup::RestoreSettings { .schemaOnly = true });
        CHECK(restoreProgress.Errors() == NoErrors);

        CHECK(CountRows(R"("lw_l7_core"."parent")") == 0);
        CHECK(CountRows(R"("lw_l7_extra"."child")") == 0);
        CHECK(ChildRejectsOrphan());
    }

    SECTION("a restore into schemas that exist leaves them and replaces the tables")
    {
        DropSchemaFixture({ { ExtraSchema, "child" }, { ExtraSchema, "shared" }, { CoreSchema, "shared" } }, {});

        ErrorRecordingProgressManager restoreProgress;
        SqlBackup::Restore(backupFile, connectionString, 1, restoreProgress);
        CHECK(restoreProgress.Errors() == NoErrors);

        CHECK(CountRows(R"("lw_l7_core"."parent")") == 2);
        CHECK(CountRows(R"("lw_l7_extra"."child")") == 3);
        CHECK(ChildRejectsOrphan());
    }

    SECTION("a target schema is refused")
    {
        DropCoreExtraFixture();

        ErrorRecordingProgressManager restoreProgress;
        SqlBackup::Restore(backupFile, connectionString, 1, restoreProgress, "elsewhere");

        CHECK(restoreProgress.Errors().contains("Restore refused"));
        CHECK(CountRows(R"("lw_l7_core"."parent")") == -1);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: a schema-only backup of two schemas restores the cross-schema foreign key",
                 "[SqlBackup][MultiSchema]")
{
    auto stmt = SqlStatement {};
    UNSUPPORTED_DATABASE(stmt, SqlServerType::SQLITE); // SQLite has no schemas to back up

    std::filesystem::path const backupFile = "l7_two_schemas_schema_only.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const fixture = ScopedCoreExtraFixture {};
    auto const& connectionString = SqlConnection::DefaultConnectionString();

    ErrorRecordingProgressManager backupProgress;
    SqlBackup::BackupSchemas(backupFile,
                             connectionString,
                             1,
                             backupProgress,
                             { std::string { CoreSchema }, std::string { ExtraSchema } },
                             "*",
                             {},
                             SqlBackup::BackupSettings { .schemaOnly = true });
    REQUIRE(backupProgress.Errors() == NoErrors);
    CHECK_FALSE(std::ranges::any_of(ArchiveEntryNames(backupFile),
                                    [](std::string const& name) { return name.starts_with("data/"); }));

    DropCoreExtraFixture();
    ErrorRecordingProgressManager restoreProgress;
    SqlBackup::Restore(backupFile, connectionString, 1, restoreProgress);
    CHECK(restoreProgress.Errors() == NoErrors);

    CHECK(CountRows(R"("lw_l7_core"."parent")") == 0);
    CHECK(CountRows(R"("lw_l7_extra"."child")") == 0);
    CHECK(CountRows(R"("lw_l7_extra"."shared")") == 0);
    CHECK(ChildRejectsOrphan());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: a multi-schema archive restores where table names are unique across its schemas",
                 "[SqlBackup][MultiSchema]")
{
    std::filesystem::path const backupFile = "l7_crafted_unique.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const cleanup = [] {
        DropSchemaFixture({ { "lw_l7_beta", "item" }, { "lw_l7_alpha", "owner" } }, { "lw_l7_beta", "lw_l7_alpha" });
    };
    cleanup();

    auto item = TableJson("lw_l7_beta", "item", "owner_id", "integer", 1);
    item["foreign_keys"].push_back({ { "name", "" },
                                     { "columns", { "owner_id" } },
                                     { "referenced_table", "owner" },
                                     { "referenced_columns", { "id" } },
                                     { "referenced_schema", "lw_l7_alpha" } });
    auto const metadata = nlohmann::json {
        { "format_version", "2.0" },
        { "schema_name", "" },
        { "schema_names", { "lw_l7_alpha", "lw_l7_beta" } },
        { "schema", nlohmann::json::array({ TableJson("lw_l7_alpha", "owner", "name", "varchar", 2), item }) },
    };
    WriteArchive(backupFile,
                 {
                     { "metadata.json", metadata.dump() },
                     { "data/lw_l7_alpha/owner/0001_00.msgpack",
                       MakeChunk({ { int64_t { 1 }, std::string { "ann" } }, { int64_t { 2 }, std::string { "bob" } } }) },
                     { "data/lw_l7_beta/item/0001_00.msgpack", MakeChunk({ { int64_t { 7 }, int64_t { 2 } } }) },
                 });

    ErrorRecordingProgressManager progress;
    SqlBackup::Restore(backupFile, SqlConnection::DefaultConnectionString(), 1, progress);

    auto stmt = SqlStatement {};
    CHECK(progress.Errors() == NoErrors);
    CHECK(CountRows(QualifiedName(stmt, "lw_l7_alpha", "owner")) == 2);
    CHECK(CountRows(QualifiedName(stmt, "lw_l7_beta", "item")) == 1);
    cleanup();
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlBackup: a multi-schema archive with a table name in two schemas needs a database with schemas",
                 "[SqlBackup][MultiSchema]")
{
    // SQLite compares table names case-insensitively, so "Item" and "item" are the same table there.
    auto const betaName = GENERATE(as<std::string> {}, "item", "Item");
    std::filesystem::path const backupFile = "l7_crafted_duplicate.zip";
    auto const backupFileCleaner = ScopedFileRemoved { backupFile };
    auto const cleanup = [&] {
        DropSchemaFixture({ { "lw_l7_beta", betaName }, { "lw_l7_alpha", "item" } }, { "lw_l7_beta", "lw_l7_alpha" });
    };
    cleanup();

    auto const metadata = nlohmann::json {
        { "format_version", "2.0" },
        { "schema_name", "" },
        { "schema_names", { "lw_l7_alpha", "lw_l7_beta" } },
        { "schema",
          nlohmann::json::array({ TableJson("lw_l7_alpha", "item", "label", "varchar", 1),
                                  TableJson("lw_l7_beta", betaName, "label", "varchar", 1) }) },
    };
    WriteArchive(backupFile,
                 {
                     { "metadata.json", metadata.dump() },
                     { "data/lw_l7_alpha/item/0001_00.msgpack", MakeChunk({ { int64_t { 1 }, std::string { "alpha" } } }) },
                     { std::format("data/lw_l7_beta/{}/0001_00.msgpack", betaName),
                       MakeChunk({ { int64_t { 1 }, std::string { "beta" } } }) },
                 });

    ErrorRecordingProgressManager progress;
    SqlBackup::Restore(backupFile, SqlConnection::DefaultConnectionString(), 1, progress);

    auto stmt = SqlStatement {};
    if (stmt.Connection().QueryFormatter().SupportsSchemas())
    {
        CHECK(progress.Errors() == NoErrors);
        CHECK(SqlStatement {}
                  .ExecuteDirectScalar<std::string>(std::format(R"(SELECT "label" FROM "lw_l7_beta"."{}")", betaName))
                  .value_or("")
              == "beta");
    }
    else
    {
        auto const errors = progress.Errors();
        CHECK(errors.contains("Restore refused: the target database has no schemas"));
        CHECK(errors.contains("item"));
        CHECK(errors.contains(betaName));
        CHECK(CountRows(R"("item")") == -1);
    }
    cleanup();
}
