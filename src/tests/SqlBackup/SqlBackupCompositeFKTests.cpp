#include "tests/Utils.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <vector>

using namespace Lightweight;
using namespace Lightweight::SqlBackup;

namespace
{

struct FKInfo
{
    int id;
    int seq;
    std::string table;
    std::string from;
    std::string to;
};

struct LambdaProgressManager: SqlBackup::ProgressManager
{
    std::function<void(SqlBackup::Progress const&)> callback;

    explicit LambdaProgressManager(std::function<void(SqlBackup::Progress const&)> cb = {}):
        callback(std::move(cb))
    {
    }

    std::mutex mutex;
    std::vector<std::string> errors;

    void Update(SqlBackup::Progress const& p) override
    {
        std::scoped_lock lock(mutex);
        if (p.state == SqlBackup::Progress::State::Error)
        {
            std::cerr << "SqlBackup Error: " << p.message << "\n";
            errors.push_back(p.message);
        }
        if (callback)
            callback(p);
    }

    void EnsureNoErrors(std::source_location sourceLocation = std::source_location::current())
    {
        std::scoped_lock lock(mutex);
        if (!errors.empty())
        {
            for (auto const& e: errors)
                UNSCOPED_INFO("SqlBackup Logic Error: " << e);
            FAIL("SqlBackup encountered errors at " << sourceLocation.file_name() << ":" << sourceLocation.line() << "");
        }
    }

    void AllDone() override {}

    void SetMaxTableNameLength(size_t) override {}
};

} // namespace

TEST_CASE("SqlBackup: Composite Foreign Keys", "[SqlBackup][ForeignKeys]")
{
    auto const testDir = std::filesystem::current_path() / "test_output" / "CompositeFK";
    if (std::filesystem::exists(testDir))
        std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);
    // Use DefaultConnectionString to respect --trace-sql / --trace-odbc or environment variable configuration
    auto const& connectionString = SqlConnection::DefaultConnectionString();

    auto const DropTables = [&](SqlConnection& conn) {
        SqlStatement stmt { conn };
        SqlTestFixture::DropAllTablesInDatabase(stmt);
    };

    // 1. Setup
    std::string schema;
    {
        SqlConnection conn;
        if (!conn.Connect(connectionString))
            FAIL("Skipping test: Could not connect to database");
        if (conn.ServerType() == SqlServerType::MICROSOFT_SQL)
            schema = "dbo";

        DropTables(conn);

        SqlStatement stmt { conn };
        try
        {
            WARN("DEBUG: Creating Parents table...");
            // MSSQL supports TEXT but VARCHAR(MAX) is preferred. Using TEXT for compatibility with existing code if
            // possible. But let's stick to standard types.
            stmt.ExecuteDirect("CREATE TABLE Parents ("
                               "  p1 INT,"
                               "  p2 INT,"
                               "  info VARCHAR(100),"
                               "  PRIMARY KEY (p1, p2)"
                               ")");

            WARN("DEBUG: Creating Children table...");
            stmt.ExecuteDirect("CREATE TABLE Children ("
                               "  c1 INT PRIMARY KEY,"
                               "  p1 INT,"
                               "  p2 INT,"
                               "  extra VARCHAR(100),"
                               "  FOREIGN KEY (p1, p2) REFERENCES Parents (p1, p2)"
                               ")");

            WARN("DEBUG: Inserting data...");
            stmt.ExecuteDirect("INSERT INTO Parents (p1, p2, info) VALUES (1, 1, 'Mom')");
            stmt.ExecuteDirect("INSERT INTO Parents (p1, p2, info) VALUES (1, 2, 'Dad')");
            stmt.ExecuteDirect("INSERT INTO Children (c1, p1, p2, extra) VALUES (100, 1, 1, 'Child1')");
            stmt.ExecuteDirect("INSERT INTO Children (c1, p1, p2, extra) VALUES (101, 1, 2, 'Child2')");
        }
        catch (std::exception const& e)
        {
            FAIL("Step 1 Failed: " << e.what());
        }
    }

    // 2. Backup
    auto const backupPath = testDir / "backup.zip";
    LambdaProgressManager progress;

    Backup(backupPath, connectionString, 1, progress, schema);
    progress.EnsureNoErrors();

    REQUIRE(std::filesystem::exists(backupPath));

    // 3. Drop tables to simulate restore target
    {
        SqlConnection conn;
        conn.Connect(connectionString);
        DropTables(conn);
    }

    // 4. Restore to same DB
    Restore(backupPath, connectionString, 1, progress, schema);
    progress.EnsureNoErrors();

    // 5. Verify Schema and Data
    {
        SqlConnection conn;
        conn.Connect(connectionString);
        if (conn.ServerType() == SqlServerType::MICROSOFT_SQL)
            schema = "dbo";

        SqlStatement stmt { conn };

        // Check data
        auto countParents = stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM Parents").value();
        REQUIRE(countParents == 2);

        auto countChildren = stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM Children").value();
        REQUIRE(countChildren == 2);

        if (conn.ServerType() == SqlServerType::MICROSOFT_SQL)
        {
            // Check Foreign Keys (MSSQL specific check)
            auto fkCount = stmt.ExecuteDirectScalar<int>(
                                   "SELECT COUNT(*) FROM sys.foreign_keys WHERE parent_object_id = OBJECT_ID('Children')")
                               .value();
            REQUIRE(fkCount == 1);

            // Detailed check on columns
            auto fkColCount =
                stmt.ExecuteDirectScalar<int>(
                        "SELECT COUNT(*) FROM sys.foreign_key_columns WHERE parent_object_id = OBJECT_ID('Children')")
                    .value();
            REQUIRE(fkColCount == 2);
        }
    }
}
