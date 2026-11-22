// SPDX-License-Identifier: Apache-2.0

#include "../ForeignThreadLocale.hpp"
#include "TestHelpers.hpp"

#include <Lightweight/SqlBackup/BatchManager.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace Lightweight;
using namespace Lightweight::detail;
using namespace Lightweight::SqlBackup;
using namespace Lightweight::SqlBackup::Tests;

TEST_CASE("BatchManager: Integration with SQLite", "[SqlBackup][Integration]")
{
    SqlConnection conn;
    if (!conn.Connect(SqlConnection::DefaultConnectionString()))
        return; // Skip if no connection

    SqlStatement stmt { conn };
    (void) stmt.ExecuteDirect("DROP TABLE IF EXISTS test_batch");
    (void) stmt.ExecuteDirect("CREATE TABLE test_batch (id INTEGER PRIMARY KEY, txt VARCHAR(255))");

    std::vector<SqlColumnDeclaration> cols = { { .name = "id", .type = SqlColumnTypeDefinitions::Integer {} },
                                               { .name = "txt", .type = SqlColumnTypeDefinitions::Text { .size = 255 } } };

    // Actual Executor
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t rowCount) {
        std::string sql = "INSERT INTO test_batch (id, txt) VALUES (?, ?)";
        stmt.Prepare(sql);
        (void) stmt.ExecuteBatch(rawCols, rowCount);
    };

    BatchManager bm(MakeHooks(executor), cols, 10);

    bm.PushRow({ 1, "Hello" });
    bm.PushRow({ 2, "World" });
    bm.PushRow({ 3, "NULL" });            // String "NULL"
    bm.PushRow({ 4, std::monostate {} }); // Real NULL
    bm.PushRow({ 5, "Line1\nLine2" });

    bm.Flush();

    // Verify
    REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM test_batch") == 5);

    REQUIRE(stmt.ExecuteDirectScalar<std::string>("SELECT txt FROM test_batch WHERE id=1") == "Hello");
    REQUIRE(stmt.ExecuteDirectScalar<std::string>("SELECT txt FROM test_batch WHERE id=2") == "World");
    REQUIRE(stmt.ExecuteDirectScalar<std::string>("SELECT txt FROM test_batch WHERE id=3") == "NULL");

    auto val4 = stmt.ExecuteDirectScalar<std::string>("SELECT txt FROM test_batch WHERE id=4");
    REQUIRE_FALSE(val4.has_value()); // Should be NULL

    REQUIRE(stmt.ExecuteDirectScalar<std::string>("SELECT txt FROM test_batch WHERE id=5") == "Line1\nLine2");
}

#if defined(_WIN32) || defined(_WIN64)
TEST_CASE("BatchManager: narrow text keeps its meaning under OEMCP and a foreign thread locale",
          "[SqlBackup][Integration][Unicode]")
{
    auto conn = SqlConnection { SqlConnectionString { SqlConnection::DefaultConnectionString().value + ";OEMCP=1" } };
    if (!conn.IsAlive() || conn.ServerType() != SqlServerType::SQLITE)
        return;

    constexpr auto unicodeText = std::u16string_view { u"\u00E4\u00F6\u00FC\u00DF\u00F1" };
    auto const narrowText = Lightweight::Tests::ToProcessCodePage(unicodeText);
    auto const threadLocale = Lightweight::Tests::ScopedForeignThreadLocale {};

    SqlStatement stmt { conn };
    (void) stmt.ExecuteDirect("DROP TABLE IF EXISTS test_batch_narrow");
    (void) stmt.ExecuteDirect("CREATE TABLE test_batch_narrow (id INTEGER PRIMARY KEY, txt VARCHAR(32), fixed CHAR(8))");

    std::vector<SqlColumnDeclaration> cols = {
        { .name = "id", .type = SqlColumnTypeDefinitions::Integer {} },
        { .name = "txt", .type = SqlColumnTypeDefinitions::Varchar { .size = 32 } },
        { .name = "fixed", .type = SqlColumnTypeDefinitions::Char { .size = 8 } },
    };
    BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t rowCount) {
        stmt.Prepare("INSERT INTO test_batch_narrow (id, txt, fixed) VALUES (?, ?, ?)");
        (void) stmt.ExecuteBatch(rawCols, rowCount);
    };
    BatchManager bm(MakeHooks(executor), cols, 10, conn.ServerType(), SqlServerType::UNKNOWN, conn.NarrowTextBindingMode());
    bm.PushRow({ 1, narrowText, narrowText });
    bm.PushRow({ 2, std::monostate {}, narrowText });
    bm.Flush();

    auto cursor = stmt.ExecuteDirect("SELECT txt, fixed FROM test_batch_narrow ORDER BY id");
    REQUIRE(cursor.FetchRow());
    CHECK(cursor.GetColumn<std::u16string>(1) == unicodeText);
    CHECK(cursor.GetColumn<std::u16string>(2) == unicodeText);
    REQUIRE(cursor.FetchRow());
    CHECK_FALSE(cursor.GetNullableColumn<std::u16string>(1).has_value());
    CHECK(cursor.GetColumn<std::u16string>(2) == unicodeText);
}
#endif
