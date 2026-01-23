// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/BatchManager.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace Lightweight;
using namespace Lightweight::detail;
using namespace Lightweight::SqlBackup;

TEST_CASE("BatchManager: Integration with SQLite", "[SqlBackup][Integration]")
{
    SqlConnection conn;
    if (!conn.Connect(SqlConnection::DefaultConnectionString()))
        return; // Skip if no connection

    SqlStatement stmt { conn };
    stmt.ExecuteDirect("DROP TABLE IF EXISTS test_batch");
    stmt.ExecuteDirect("CREATE TABLE test_batch (id INTEGER PRIMARY KEY, txt VARCHAR(255))");

    std::vector<SqlColumnDeclaration> cols = { { "id", SqlColumnTypeDefinitions::Integer {} },
                                               { "txt", SqlColumnTypeDefinitions::Text { .size = 255 } } };

    // Actual Executor
    BatchManager::BatchExecutor executor = [&](std::vector<SqlRawColumn> const& rawCols, size_t rowCount) {
        std::string sql = "INSERT INTO test_batch (id, txt) VALUES (?, ?)";
        stmt.Prepare(sql);
        stmt.ExecuteBatch(rawCols, rowCount);
    };

    BatchManager bm(executor, cols, 10);

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
