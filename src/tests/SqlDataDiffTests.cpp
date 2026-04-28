// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlDataDiff.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>

using namespace Lightweight;
using namespace Lightweight::SqlSchema;

namespace
{

/// Removes the file at @p path on construction and again on destruction. Lets each test
/// start from a clean slate without leaking files between runs.
struct ScopedTempFile
{
    std::filesystem::path path;

    explicit ScopedTempFile(std::filesystem::path p):
        path(std::move(p))
    {
        std::filesystem::remove(path);
    }
    ~ScopedTempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    ScopedTempFile(ScopedTempFile const&) = delete;
    ScopedTempFile& operator=(ScopedTempFile const&) = delete;
    ScopedTempFile(ScopedTempFile&&) = delete;
    ScopedTempFile& operator=(ScopedTempFile&&) = delete;
};

[[nodiscard]] SqlConnectionString SqliteConn(std::filesystem::path const& path)
{
    // The Windows ODBC driver name has spaces and must be wrapped in braces; the
    // unixODBC convention used on Linux/macOS keeps a bare identifier.
#if defined(_WIN32) || defined(_WIN64)
    constexpr auto driverName = "{SQLite3 ODBC Driver}";
#else
    constexpr auto driverName = "SQLite3";
#endif
    return SqlConnectionString { std::format("DRIVER={};Database={}", driverName, path.string()) };
}

/// Creates a `users(id PK, name, email)` table and inserts the given rows.
void SetupUsersTable(SqlConnection& conn, std::vector<std::tuple<int, std::string, std::string>> const& rows)
{
    auto stmt = SqlStatement { conn };
    (void) stmt.ExecuteDirect("DROP TABLE IF EXISTS users");
    (void) stmt.ExecuteDirect("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL, email TEXT NOT NULL)");
    stmt.Prepare("INSERT INTO users (id, name, email) VALUES (?, ?, ?)");
    for (auto const& [id, name, email]: rows)
        (void) stmt.Execute(id, name, email);
}

/// Reads the schema of @p tableName from @p conn (using SqlSchema::ReadAllTables) and
/// returns the matching Table — REQUIRE-fails if not present.
[[nodiscard]] Table FetchTableSchema(SqlConnection& conn, std::string_view tableName)
{
    auto stmt = SqlStatement { conn };
    auto const list = SqlSchema::ReadAllTables(stmt, conn.DatabaseName(), "");
    auto const it = std::ranges::find_if(list, [&](Table const& t) { return t.name == tableName; });
    REQUIRE(it != list.end());
    return *it;
}

} // namespace

TEST_CASE("DiffTableData: identical tables produce no row diffs", "[SqlDataDiff]")
{
    auto const tmp = std::filesystem::temp_directory_path();
    auto const guardA = ScopedTempFile { tmp / "diff_data_identical_a.sqlite" };
    auto const guardB = ScopedTempFile { tmp / "diff_data_identical_b.sqlite" };

    auto connA = SqlConnection { SqliteConn(guardA.path) };
    auto connB = SqlConnection { SqliteConn(guardB.path) };
    REQUIRE(connA.IsAlive());
    REQUIRE(connB.IsAlive());

    auto const rows = std::vector<std::tuple<int, std::string, std::string>> {
        { 1, "alice", "a@x" },
        { 2, "bob", "b@x" },
    };
    SetupUsersTable(connA, rows);
    SetupUsersTable(connB, rows);

    auto const schema = FetchTableSchema(connA, "users");
    auto const diff = DiffTableData(connA, connB, schema, schema);

    CHECK(diff.rows.empty());
    CHECK(diff.aRowCount == 2);
    CHECK(diff.bRowCount == 2);
    CHECK_FALSE(diff.skipReason.has_value());
}

TEST_CASE("DiffTableData: detects added, removed, and changed rows", "[SqlDataDiff]")
{
    auto const tmp = std::filesystem::temp_directory_path();
    auto const guardA = ScopedTempFile { tmp / "diff_data_drift_a.sqlite" };
    auto const guardB = ScopedTempFile { tmp / "diff_data_drift_b.sqlite" };

    auto connA = SqlConnection { SqliteConn(guardA.path) };
    auto connB = SqlConnection { SqliteConn(guardB.path) };

    SetupUsersTable(connA,
                    {
                        { 1, "alice", "a@x" },
                        { 2, "bob", "b@x" },
                        { 3, "carol", "c@x" }, // only in A (removed in B)
                    });
    SetupUsersTable(connB,
                    {
                        { 1, "alice", "a@x" },
                        { 2, "bob", "b@y" },  // changed: email differs
                        { 4, "dave", "d@x" }, // only in B (added in B)
                    });

    auto const schema = FetchTableSchema(connA, "users");
    auto const diff = DiffTableData(connA, connB, schema, schema);

    REQUIRE(diff.rows.size() == 3);

    auto const findByPk = [&](std::string const& pkVal) -> RowDiff const* {
        auto const it = std::ranges::find_if(
            diff.rows, [&](RowDiff const& r) { return !r.primaryKey.empty() && r.primaryKey.front() == pkVal; });
        return it == diff.rows.end() ? nullptr : &*it;
    };

    auto const* changed = findByPk("2");
    REQUIRE(changed);
    CHECK(changed->kind == DiffKind::Changed);
    REQUIRE(changed->changedCells.size() == 1);
    CHECK(std::get<0>(changed->changedCells.front()) == "email");
    CHECK(std::get<1>(changed->changedCells.front()) == "b@x");
    CHECK(std::get<2>(changed->changedCells.front()) == "b@y");

    auto const* removed = findByPk("3");
    REQUIRE(removed);
    CHECK(removed->kind == DiffKind::OnlyInA);

    auto const* added = findByPk("4");
    REQUIRE(added);
    CHECK(added->kind == DiffKind::OnlyInB);
}

TEST_CASE("DiffTableData: tables without a primary key are skipped", "[SqlDataDiff]")
{
    auto const tmp = std::filesystem::temp_directory_path();
    auto const guardA = ScopedTempFile { tmp / "diff_data_nopk_a.sqlite" };
    auto const guardB = ScopedTempFile { tmp / "diff_data_nopk_b.sqlite" };

    auto connA = SqlConnection { SqliteConn(guardA.path) };
    auto connB = SqlConnection { SqliteConn(guardB.path) };

    auto stmtA = SqlStatement { connA };
    auto stmtB = SqlStatement { connB };
    (void) stmtA.ExecuteDirect("CREATE TABLE log (msg TEXT)");
    (void) stmtA.ExecuteDirect("INSERT INTO log (msg) VALUES ('hello')");
    (void) stmtB.ExecuteDirect("CREATE TABLE log (msg TEXT)");

    // Hand-build a Table with no primaryKeys. (SQLite reflects rowid PKs but we want the
    // explicit no-PK case here; the function only checks tableSchema.primaryKeys.)
    auto schema = Table {};
    schema.name = "log";
    schema.columns.push_back(Column { .name = "msg", .dialectDependantTypeString = "TEXT" });

    auto const diff = DiffTableData(connA, connB, schema, schema);
    REQUIRE(diff.skipReason.has_value());
    CHECK(diff.skipReason.value_or("") == "no primary key");
    CHECK(diff.rows.empty());
}

TEST_CASE("DiffTableData: each side uses its own descriptor to qualify the SELECT", "[SqlDataDiff]")
{
    // Regression for cross-engine diff: a postgres-vs-mssql diff failed with
    // "Invalid object name 'public.schema_migrations'" because the data-diff SELECT
    // was built once with side A's schema label and reused on side B. The fix takes
    // one descriptor per side and qualifies each query with its own schema/name.
    //
    // We pin this with two SQLite databases that store the same logical table under
    // different names. With the old single-descriptor API, side B's query would have
    // referenced side A's name and failed; with the per-side API, each query resolves
    // on its own connection.
    auto const tmp = std::filesystem::temp_directory_path();
    auto const guardA = ScopedTempFile { tmp / "diff_data_perside_a.sqlite" };
    auto const guardB = ScopedTempFile { tmp / "diff_data_perside_b.sqlite" };

    auto connA = SqlConnection { SqliteConn(guardA.path) };
    auto connB = SqlConnection { SqliteConn(guardB.path) };

    // Side A holds the table as `users`; side B as `users_b`. Same shape, different name.
    auto const rows = std::vector<std::tuple<int, std::string, std::string>> {
        { 1, "alice", "a@x" },
        { 2, "bob", "b@x" },
    };
    SetupUsersTable(connA, rows);
    {
        auto stmt = SqlStatement { connB };
        (void) stmt.ExecuteDirect("DROP TABLE IF EXISTS users_b");
        (void) stmt.ExecuteDirect("CREATE TABLE users_b (id INTEGER PRIMARY KEY, name TEXT NOT NULL, email TEXT NOT NULL)");
        stmt.Prepare("INSERT INTO users_b (id, name, email) VALUES (?, ?, ?)");
        for (auto const& [id, name, email]: rows)
            (void) stmt.Execute(id, name, email);
    }

    auto const schemaA = FetchTableSchema(connA, "users");
    auto const schemaB = FetchTableSchema(connB, "users_b");
    auto const diff = DiffTableData(connA, connB, schemaA, schemaB);

    CHECK_FALSE(diff.skipReason.has_value());
    CHECK(diff.rows.empty());
    CHECK(diff.aRowCount == 2);
    CHECK(diff.bRowCount == 2);
}

TEST_CASE("DiffTableData: progress callback fires", "[SqlDataDiff]")
{
    auto const tmp = std::filesystem::temp_directory_path();
    auto const guardA = ScopedTempFile { tmp / "diff_data_progress_a.sqlite" };
    auto const guardB = ScopedTempFile { tmp / "diff_data_progress_b.sqlite" };

    auto connA = SqlConnection { SqliteConn(guardA.path) };
    auto connB = SqlConnection { SqliteConn(guardB.path) };

    // Empty rows on both sides — but the final "force" report at end of scan must still fire
    // exactly once (the rate-limited mid-scan reports won't trigger with no rows).
    SetupUsersTable(connA, {});
    SetupUsersTable(connB, {});

    auto const schema = FetchTableSchema(connA, "users");
    auto callCount = 0;
    auto const diff = DiffTableData(connA, connB, schema, schema, 0, [&](DiffProgressEvent const&) { ++callCount; });

    CHECK(diff.rows.empty());
    CHECK(callCount >= 1);
}
