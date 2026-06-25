// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlServerType.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace Lightweight;

// ================================================================================================
// SqlQueryFormatter::Get — dispatch from SqlServerType to the singleton formatter
// ================================================================================================

TEST_CASE("SqlQueryFormatter::Get returns the matching formatter for each supported DBMS", "[SqlQueryFormatter]")
{
    CHECK(SqlQueryFormatter::Get(SqlServerType::MICROSOFT_SQL) == &SqlQueryFormatter::SqlServer());
    CHECK(SqlQueryFormatter::Get(SqlServerType::POSTGRESQL) == &SqlQueryFormatter::PostgrSQL());
    CHECK(SqlQueryFormatter::Get(SqlServerType::SQLITE) == &SqlQueryFormatter::Sqlite());
}

TEST_CASE("SqlQueryFormatter::Get returns nullptr for the unsupported entries", "[SqlQueryFormatter]")
{
    // UNKNOWN and MYSQL have no first-class formatter — the table holds nullptr.
    CHECK(SqlQueryFormatter::Get(SqlServerType::UNKNOWN) == nullptr);
    CHECK(SqlQueryFormatter::Get(SqlServerType::MYSQL) == nullptr);
}

TEST_CASE("SqlQueryFormatter accessors return the same singleton on every call", "[SqlQueryFormatter]")
{
    CHECK(&SqlQueryFormatter::Sqlite() == &SqlQueryFormatter::Sqlite());
    CHECK(&SqlQueryFormatter::SqlServer() == &SqlQueryFormatter::SqlServer());
    CHECK(&SqlQueryFormatter::PostgrSQL() == &SqlQueryFormatter::PostgrSQL());
    // …and the three accessors return distinct singletons.
    CHECK(&SqlQueryFormatter::Sqlite() != &SqlQueryFormatter::SqlServer());
    CHECK(&SqlQueryFormatter::Sqlite() != &SqlQueryFormatter::PostgrSQL());
    CHECK(&SqlQueryFormatter::SqlServer() != &SqlQueryFormatter::PostgrSQL());
}

// ================================================================================================
// BooleanLiteral — diverges per DBMS (SQLite/PG: TRUE/FALSE keywords, SQL Server: 1/0)
// ================================================================================================

TEST_CASE("SqlQueryFormatter::BooleanLiteral differs per DBMS", "[SqlQueryFormatter]")
{
    CHECK(SqlQueryFormatter::Sqlite().BooleanLiteral(true) == "TRUE");
    CHECK(SqlQueryFormatter::Sqlite().BooleanLiteral(false) == "FALSE");

    // PostgreSQL inherits the SQLite BooleanLiteral override (TRUE/FALSE).
    CHECK(SqlQueryFormatter::PostgrSQL().BooleanLiteral(true) == "TRUE");
    CHECK(SqlQueryFormatter::PostgrSQL().BooleanLiteral(false) == "FALSE");

    CHECK(SqlQueryFormatter::SqlServer().BooleanLiteral(true) == "1");
    CHECK(SqlQueryFormatter::SqlServer().BooleanLiteral(false) == "0");
}

// ================================================================================================
// DateFunction — every backend has a different current-date function
// ================================================================================================

TEST_CASE("SqlQueryFormatter::DateFunction emits the per-DBMS current-date keyword", "[SqlQueryFormatter]")
{
    CHECK(SqlQueryFormatter::Sqlite().DateFunction() == "date()");
    CHECK(SqlQueryFormatter::PostgrSQL().DateFunction() == "CURRENT_DATE");
    CHECK(SqlQueryFormatter::SqlServer().DateFunction() == "GETDATE()");
}

// ================================================================================================
// StringLiteral (string_view) — quote single quotes by doubling them
// ================================================================================================

// SQLite and PostgreSQL emit plain `'…'` literals. SQL Server emits the Unicode-safe `N'…'`
// form so non-ASCII codepoints round-trip into NVARCHAR columns without collation surprises;
// the N-prefix is harmless for ASCII payloads but required for anything outside CP1252.
TEST_CASE("SqlQueryFormatter::StringLiteral wraps in single quotes", "[SqlQueryFormatter]")
{
    CHECK(SqlQueryFormatter::Sqlite().StringLiteral(std::string_view { "hello" }) == "'hello'");
    CHECK(SqlQueryFormatter::PostgrSQL().StringLiteral(std::string_view { "hello" }) == "'hello'");
    CHECK(SqlQueryFormatter::SqlServer().StringLiteral(std::string_view { "hello" }) == "N'hello'");
}

TEST_CASE("SqlQueryFormatter::StringLiteral collapses to two single quotes for empty input", "[SqlQueryFormatter]")
{
    CHECK(SqlQueryFormatter::Sqlite().StringLiteral(std::string_view {}) == "''");
    CHECK(SqlQueryFormatter::PostgrSQL().StringLiteral(std::string_view {}) == "''");
    CHECK(SqlQueryFormatter::SqlServer().StringLiteral(std::string_view {}) == "N''");
}

TEST_CASE("SqlQueryFormatter::StringLiteral escapes embedded single quotes by doubling", "[SqlQueryFormatter]")
{
    // ODBC SQL escapes ' as '' inside a quoted literal; the SQL Server N-prefix sits
    // outside the quote run, so escaping rules inside the quotes are identical.
    CHECK(SqlQueryFormatter::Sqlite().StringLiteral(std::string_view { "O'Brien" }) == "'O''Brien'");
    CHECK(SqlQueryFormatter::PostgrSQL().StringLiteral(std::string_view { "O'Brien" }) == "'O''Brien'");
    CHECK(SqlQueryFormatter::SqlServer().StringLiteral(std::string_view { "O'Brien" }) == "N'O''Brien'");

    // Run of quotes at boundary positions.
    CHECK(SqlQueryFormatter::Sqlite().StringLiteral(std::string_view { "''" }) == "''''''");
}

TEST_CASE("SqlQueryFormatter::StringLiteral(char) handles the single-quote special case", "[SqlQueryFormatter]")
{
    CHECK(SqlQueryFormatter::Sqlite().StringLiteral('a') == "'a'");
    CHECK(SqlQueryFormatter::Sqlite().StringLiteral('\'') == "''''");
    CHECK(SqlQueryFormatter::PostgrSQL().StringLiteral('\'') == "''''");
    CHECK(SqlQueryFormatter::SqlServer().StringLiteral('\'') == "N''''");
}

// ================================================================================================
// BinaryLiteral — encodes a byte payload using each DBMS's native hex literal syntax
// ================================================================================================

TEST_CASE("SqlQueryFormatter::BinaryLiteral renders the per-DBMS hex literal", "[SqlQueryFormatter]")
{
    std::array<uint8_t, 4> const bytes { 0xDE, 0xAD, 0xBE, 0xEF };
    std::span<uint8_t const> const data { bytes };

    CHECK(SqlQueryFormatter::Sqlite().BinaryLiteral(data) == "X'DEADBEEF'");
    CHECK(SqlQueryFormatter::PostgrSQL().BinaryLiteral(data) == R"('\xDEADBEEF')");
    CHECK(SqlQueryFormatter::SqlServer().BinaryLiteral(data) == "0xDEADBEEF");
}

TEST_CASE("SqlQueryFormatter::BinaryLiteral handles the empty payload", "[SqlQueryFormatter]")
{
    std::span<uint8_t const> const empty;
    CHECK(SqlQueryFormatter::Sqlite().BinaryLiteral(empty) == "X''");
    CHECK(SqlQueryFormatter::PostgrSQL().BinaryLiteral(empty) == R"('\x')");
    CHECK(SqlQueryFormatter::SqlServer().BinaryLiteral(empty) == "0x");
}

TEST_CASE("SqlQueryFormatter::BinaryLiteral uses upper-case hex digits", "[SqlQueryFormatter]")
{
    std::array<uint8_t, 1> const bytes { 0xAB };
    auto const sqlite = SqlQueryFormatter::Sqlite().BinaryLiteral(std::span<uint8_t const> { bytes });
    CHECK(sqlite.contains("AB"));
    CHECK_FALSE(sqlite.contains("ab"));
}

// ================================================================================================
// QualifiedTableName — quoting convention diverges between SQL Server (brackets) and the rest
// ================================================================================================

TEST_CASE("SqlQueryFormatter::QualifiedTableName quotes per-DBMS", "[SqlQueryFormatter]")
{
    // SQLite/PG: double-quoted identifiers.
    CHECK(SqlQueryFormatter::Sqlite().QualifiedTableName({}, "Users") == R"("Users")");
    CHECK(SqlQueryFormatter::Sqlite().QualifiedTableName("dbo", "Users") == R"("dbo"."Users")");
    CHECK(SqlQueryFormatter::PostgrSQL().QualifiedTableName("public", "Users") == R"("public"."Users")");

    // SQL Server: bracket identifiers.
    CHECK(SqlQueryFormatter::SqlServer().QualifiedTableName({}, "Users") == "[Users]");
    CHECK(SqlQueryFormatter::SqlServer().QualifiedTableName("dbo", "Users") == "[dbo].[Users]");
}

// ================================================================================================
// QueryLastInsertId — also fundamentally per-DBMS
// ================================================================================================

TEST_CASE("SqlQueryFormatter::QueryLastInsertId emits the per-DBMS form", "[SqlQueryFormatter]")
{
    CHECK(SqlQueryFormatter::Sqlite().QueryLastInsertId("Users") == "SELECT LAST_INSERT_ROWID()");
    CHECK(SqlQueryFormatter::PostgrSQL().QueryLastInsertId("Users") == "SELECT lastval();");
    CHECK(SqlQueryFormatter::SqlServer().QueryLastInsertId("Users") == "SELECT @@IDENTITY");
}

// ================================================================================================
// QueryServerVersion
// ================================================================================================

TEST_CASE("SqlQueryFormatter::QueryServerVersion emits the per-DBMS version query", "[SqlQueryFormatter]")
{
    CHECK(SqlQueryFormatter::Sqlite().QueryServerVersion() == "SELECT sqlite_version()");
    CHECK(SqlQueryFormatter::PostgrSQL().QueryServerVersion() == "SELECT version()");
    CHECK(SqlQueryFormatter::SqlServer().QueryServerVersion() == "SELECT @@VERSION");
}

// ================================================================================================
// RequiresTableRebuildForSchemaChange — SQLite needs a full table rebuild, others don't
// ================================================================================================

TEST_CASE("SqlQueryFormatter::RequiresTableRebuildForSchemaChange flag matches the dialect", "[SqlQueryFormatter]")
{
    CHECK(SqlQueryFormatter::Sqlite().RequiresTableRebuildForSchemaChange());
    CHECK_FALSE(SqlQueryFormatter::PostgrSQL().RequiresTableRebuildForSchemaChange());
    CHECK_FALSE(SqlQueryFormatter::SqlServer().RequiresTableRebuildForSchemaChange());
}

// ================================================================================================
// BuildForeignKeyConstraintName — static helper, not per-DBMS, but format-load-bearing
// ================================================================================================

TEST_CASE("BuildForeignKeyConstraintName for a single-column FK produces FK_<table>_<col>", "[SqlQueryFormatter]")
{
    std::array<std::string_view, 1> const cols { "user_id" };
    CHECK(SqlQueryFormatter::BuildForeignKeyConstraintName("Posts", cols) == "FK_Posts_user_id");
}

TEST_CASE("BuildForeignKeyConstraintName for a composite FK joins every column with underscores", "[SqlQueryFormatter]")
{
    std::vector<std::string_view> const cols { "tenant_id", "user_id" };
    CHECK(SqlQueryFormatter::BuildForeignKeyConstraintName("Posts", cols) == "FK_Posts_tenant_id_user_id");
}

TEST_CASE("BuildForeignKeyConstraintName with no columns degenerates to FK_<table>", "[SqlQueryFormatter]")
{
    std::vector<std::string_view> const cols {};
    CHECK(SqlQueryFormatter::BuildForeignKeyConstraintName("Posts", cols) == "FK_Posts");
}

TEST_CASE("BuildForeignKeyConstraintName composes distinct names for prefix-colliding FKs", "[SqlQueryFormatter]")
{
    // Anti-collision rationale spelled out in the SqlQueryFormatter doc-comment:
    // a single-column FK on `user_id` and a composite FK starting with `user_id`
    // must produce different constraint names so MSSQL's per-DB uniqueness holds.
    std::array<std::string_view, 1> const single { "user_id" };
    std::array<std::string_view, 2> const composite { "user_id", "tenant_id" };
    CHECK(SqlQueryFormatter::BuildForeignKeyConstraintName("Posts", single)
          != SqlQueryFormatter::BuildForeignKeyConstraintName("Posts", composite));
}

// ================================================================================================
// DropTable — verify the dialect-specific SQL the formatter emits without a database
// ================================================================================================

TEST_CASE("SqlQueryFormatter::DropTable on SQLite emits a single statement and ignores schema/cascade",
          "[SqlQueryFormatter]")
{
    auto const sqls = SqlQueryFormatter::Sqlite().DropTable("ignoredSchema", "Posts", /*ifExists=*/false, /*cascade=*/true);
    REQUIRE(sqls.size() == 1);
    // SQLite has no schema concept here — the schema argument is silently dropped, and the
    // emitted statement uses just the bare quoted table name.
    CHECK(sqls.front() == R"(DROP TABLE "Posts";)");
}

TEST_CASE("SqlQueryFormatter::DropTable on SQLite honours ifExists", "[SqlQueryFormatter]")
{
    auto const sqls = SqlQueryFormatter::Sqlite().DropTable({}, "Posts", /*ifExists=*/true, /*cascade=*/false);
    REQUIRE(sqls.size() == 1);
    CHECK(sqls.front() == R"(DROP TABLE IF EXISTS "Posts";)");
}

TEST_CASE("SqlQueryFormatter::DropTable on PostgreSQL appends CASCADE only when requested", "[SqlQueryFormatter]")
{
    auto const plain = SqlQueryFormatter::PostgrSQL().DropTable({}, "Posts", /*ifExists=*/false, /*cascade=*/false);
    REQUIRE(plain.size() == 1);
    CHECK_FALSE(plain.front().contains("CASCADE"));

    auto const cascaded = SqlQueryFormatter::PostgrSQL().DropTable({}, "Posts", /*ifExists=*/false, /*cascade=*/true);
    REQUIRE(cascaded.size() == 1);
    CHECK(cascaded.front().contains("CASCADE"));
}

TEST_CASE("SqlQueryFormatter::DropTable on SQL Server with cascade emits a two-statement plan", "[SqlQueryFormatter]")
{
    // SQL Server can't express FK cascade inline — the formatter emits a separate
    // dynamic SQL statement that drops referencing FKs before the table itself.
    auto const sqls = SqlQueryFormatter::SqlServer().DropTable({}, "Posts", /*ifExists=*/true, /*cascade=*/true);
    REQUIRE(sqls.size() == 2);
    CHECK(sqls[0].contains("sys.foreign_keys"));
    CHECK(sqls[1].starts_with("DROP TABLE IF EXISTS"));
}
