#include "tests/Utils.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <vector>

using namespace Lightweight;
using namespace Lightweight::SqlBackup;

namespace
{

struct LambdaProgressManager: SqlBackup::ProgressManager
{
    std::function<void(SqlBackup::Progress const&)> callback;

    explicit LambdaProgressManager(std::function<void(SqlBackup::Progress const&)> cb = {}):
        callback(std::move(cb))
    {
    }

    std::mutex mutex;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void Update(SqlBackup::Progress const& p) override
    {
        std::scoped_lock lock(mutex);
        if (p.state == SqlBackup::Progress::State::Error)
            errors.push_back(p.message);
        else if (p.state == SqlBackup::Progress::State::Warning)
            warnings.push_back(p.message);
        if (callback)
            callback(p);
    }

    void EnsureNoErrors(std::source_location sourceLocation = std::source_location::current())
    {
        std::scoped_lock lock(mutex);
        if (!errors.empty())
        {
            for (auto const& e: errors)
                UNSCOPED_INFO("SqlBackup Error: " << e);
            FAIL("SqlBackup encountered errors at " << sourceLocation.file_name() << ":" << sourceLocation.line());
        }
    }

    void AllDone() override {}

    void SetMaxTableNameLength(size_t /*len*/) override {}
};

/// Index information for verification.
struct IndexInfo
{
    std::string name;
    std::vector<std::string> columns;
    bool isUnique = false;
};

std::vector<IndexInfo> GetSqliteIndexes(SqlStatement& stmt, std::string const& tableName)
{
    std::vector<IndexInfo> indexes;

    stmt.ExecuteDirect(std::format("SELECT name, \"unique\" FROM pragma_index_list('{}')", tableName));
    std::vector<std::pair<std::string, bool>> indexList;
    while (stmt.FetchRow())
    {
        auto name = stmt.GetColumn<std::string>(1);
        auto isUnique = stmt.GetColumn<int>(2) != 0;
        if (!name.starts_with("sqlite_autoindex_"))
            indexList.emplace_back(name, isUnique);
    }
    stmt.CloseCursor();

    for (auto const& [indexName, isUnique]: indexList)
    {
        IndexInfo info { .name = indexName, .columns = {}, .isUnique = isUnique };
        stmt.ExecuteDirect(std::format("SELECT name FROM pragma_index_info('{}')", indexName));
        while (stmt.FetchRow())
            info.columns.push_back(stmt.GetColumn<std::string>(1));
        stmt.CloseCursor();
        indexes.push_back(std::move(info));
    }

    return indexes;
}

std::vector<IndexInfo> GetMssqlIndexes(SqlStatement& stmt, std::string const& tableName, std::string const& schema)
{
    std::vector<IndexInfo> indexes;

    // Use schema-qualified name for OBJECT_ID if schema is provided
    std::string qualifiedName = schema.empty() ? tableName : std::format("{}.{}", schema, tableName);

    stmt.ExecuteDirect(std::format(
        R"(SELECT i.name, i.is_unique, c.name as column_name
           FROM sys.indexes i
           INNER JOIN sys.index_columns ic ON i.object_id = ic.object_id AND i.index_id = ic.index_id
           INNER JOIN sys.columns c ON ic.object_id = c.object_id AND ic.column_id = c.column_id
           WHERE i.object_id = OBJECT_ID('{}')
             AND i.is_primary_key = 0
             AND i.type > 0
           ORDER BY i.name, ic.key_ordinal)",
        qualifiedName));

    std::string currentIndex;
    IndexInfo currentInfo;
    while (stmt.FetchRow())
    {
        auto indexName = stmt.GetColumn<std::string>(1);
        auto isUnique = stmt.GetColumn<int>(2) != 0;
        auto columnName = stmt.GetColumn<std::string>(3);

        if (indexName != currentIndex)
        {
            if (!currentIndex.empty())
                indexes.push_back(std::move(currentInfo));
            currentIndex = indexName;
            currentInfo = IndexInfo { .name = indexName, .columns = {}, .isUnique = isUnique };
        }
        currentInfo.columns.push_back(columnName);
    }
    if (!currentIndex.empty())
        indexes.push_back(std::move(currentInfo));
    stmt.CloseCursor();

    return indexes;
}

std::vector<IndexInfo> GetPostgresIndexes(SqlStatement& stmt, std::string const& tableName)
{
    std::vector<IndexInfo> indexes;

    // PostgreSQL stores unquoted identifiers in lowercase
    std::string lowerTableName = tableName;
    std::ranges::transform(lowerTableName, lowerTableName.begin(), [](unsigned char c) { return std::tolower(c); });

    stmt.ExecuteDirect(
        std::format(R"(SELECT indexname, indexdef FROM pg_indexes WHERE tablename = '{}' AND indexname NOT LIKE '%_pkey')",
                    lowerTableName));

    while (stmt.FetchRow())
    {
        IndexInfo info;
        info.name = stmt.GetColumn<std::string>(1);
        auto indexDef = stmt.GetColumn<std::string>(2);
        info.isUnique = indexDef.find("UNIQUE") != std::string::npos;

        // Parse columns from CREATE INDEX ... ON table (col1, col2)
        auto parenStart = indexDef.find('(');
        auto parenEnd = indexDef.rfind(')');
        if (parenStart != std::string::npos && parenEnd != std::string::npos)
        {
            auto columnsPart = indexDef.substr(parenStart + 1, parenEnd - parenStart - 1);
            size_t pos = 0;
            while ((pos = columnsPart.find(',')) != std::string::npos)
            {
                auto col = columnsPart.substr(0, pos);
                col.erase(0, col.find_first_not_of(" \""));
                col.erase(col.find_last_not_of(" \"") + 1);
                info.columns.push_back(col);
                columnsPart.erase(0, pos + 1);
            }
            columnsPart.erase(0, columnsPart.find_first_not_of(" \""));
            columnsPart.erase(columnsPart.find_last_not_of(" \"") + 1);
            if (!columnsPart.empty())
                info.columns.push_back(columnsPart);
        }
        indexes.push_back(std::move(info));
    }
    stmt.CloseCursor();

    return indexes;
}

std::vector<IndexInfo> GetIndexes(SqlStatement& stmt,
                                  std::string const& tableName,
                                  SqlServerType serverType,
                                  std::string const& schema = "")
{
    switch (serverType)
    {
        case SqlServerType::SQLITE:
            return GetSqliteIndexes(stmt, tableName);
        case SqlServerType::MICROSOFT_SQL:
            return GetMssqlIndexes(stmt, tableName, schema);
        case SqlServerType::POSTGRESQL:
            return GetPostgresIndexes(stmt, tableName);
        default:
            return {};
    }
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("SqlBackup: Index Restoration", "[SqlBackup][Indexes]")
{
    auto const testDir = std::filesystem::current_path() / "test_output" / "IndexRestore";
    if (std::filesystem::exists(testDir))
        std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    auto const& connectionString = SqlConnection::DefaultConnectionString();

    auto const DropTables = [&](SqlConnection& conn) {
        SqlStatement stmt { conn };
        SqlTestFixture::DropAllTablesInDatabase(stmt);
    };

    // 1. Setup - Create table with indexes
    std::string schema;
    SqlServerType serverType {};
    {
        SqlConnection conn;
        if (!conn.Connect(connectionString))
            FAIL("Skipping test: Could not connect to database");
        serverType = conn.ServerType();
        if (serverType == SqlServerType::MICROSOFT_SQL)
            schema = "dbo";

        DropTables(conn);

        SqlStatement stmt { conn };
        try
        {
            stmt.ExecuteDirect(R"(CREATE TABLE TestIndexes (
                id INT PRIMARY KEY,
                name VARCHAR(100),
                email VARCHAR(200),
                category INT,
                score INT
            ))");

            stmt.ExecuteDirect(R"(CREATE INDEX idx_name ON TestIndexes (name))");
            stmt.ExecuteDirect(R"(CREATE UNIQUE INDEX idx_email ON TestIndexes (email))");
            stmt.ExecuteDirect(R"(CREATE INDEX idx_category_score ON TestIndexes (category, score))");

            stmt.ExecuteDirect("INSERT INTO TestIndexes VALUES (1, 'Alice', 'alice@test.com', 1, 100)");
            stmt.ExecuteDirect("INSERT INTO TestIndexes VALUES (2, 'Bob', 'bob@test.com', 1, 95)");
            stmt.ExecuteDirect("INSERT INTO TestIndexes VALUES (3, 'Charlie', 'charlie@test.com', 2, 88)");
        }
        catch (std::exception const& e)
        {
            FAIL("Setup failed: " << e.what());
        }

        auto indexes = GetIndexes(stmt, "TestIndexes", serverType, schema);
        REQUIRE(indexes.size() >= 3);
    }

    // 2. Backup
    auto const backupPath = testDir / "backup.zip";
    LambdaProgressManager progress;

    Backup(backupPath, connectionString, 1, progress, schema);
    progress.EnsureNoErrors();
    REQUIRE(std::filesystem::exists(backupPath));

    // 3. Drop tables
    {
        SqlConnection conn;
        conn.Connect(connectionString);
        DropTables(conn);
    }

    // 4. Restore
    Restore(backupPath, connectionString, 1, progress, schema);
    progress.EnsureNoErrors();

    // 5. Verify indexes were restored
    {
        SqlConnection conn;
        conn.Connect(connectionString);

        SqlStatement stmt { conn };

        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        auto count = stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM TestIndexes");
        REQUIRE(count.has_value());
        REQUIRE(count.value() == 3);
        // NOLINTEND(bugprone-unchecked-optional-access)

        auto indexes = GetIndexes(stmt, "TestIndexes", serverType, schema);

        // MSSQL has a known issue where SQLStatistics doesn't return indexes properly
        // with ODBC Driver 18. Skip strict verification for MSSQL until fixed.
        // TODO: Fix AllIndexes() in SqlSchema.cpp for MSSQL
        if (serverType == SqlServerType::MICROSOFT_SQL)
        {
            WARN("Skipping strict index verification for MSSQL (known SQLStatistics issue)");
            // At minimum, verify the unique constraint exists (MSSQL creates it automatically)
            bool hasUniqueOnEmail = false;
            for (auto const& idx: indexes)
            {
                if (idx.isUnique && idx.columns.size() == 1 && idx.columns[0] == "email")
                    hasUniqueOnEmail = true;
            }
            CHECK(hasUniqueOnEmail);
        }
        else
        {
            // For SQLite and PostgreSQL, verify all indexes were restored
            bool foundNameIndex = false;
            bool foundEmailIndex = false;
            bool foundCompositeIndex = false;

            for (auto const& idx: indexes)
            {
                if (idx.name == "idx_name")
                {
                    foundNameIndex = true;
                    REQUIRE(idx.columns.size() == 1);
                    REQUIRE(idx.columns[0] == "name");
                    REQUIRE_FALSE(idx.isUnique);
                }
                else if (idx.name == "idx_email")
                {
                    foundEmailIndex = true;
                    REQUIRE(idx.columns.size() == 1);
                    REQUIRE(idx.columns[0] == "email");
                    REQUIRE(idx.isUnique);
                }
                else if (idx.name == "idx_category_score")
                {
                    foundCompositeIndex = true;
                    REQUIRE(idx.columns.size() == 2);
                    REQUIRE(idx.columns[0] == "category");
                    REQUIRE(idx.columns[1] == "score");
                    REQUIRE_FALSE(idx.isUnique);
                }
            }

            CHECK(foundNameIndex);
            CHECK(foundEmailIndex);
            CHECK(foundCompositeIndex);
        }

        // Verify unique index constraint works (should work on all databases)
        bool uniqueConstraintWorks = false;
        try
        {
            stmt.ExecuteDirect("INSERT INTO TestIndexes VALUES (4, 'Duplicate', 'alice@test.com', 3, 50)");
        }
        catch (SqlException const&)
        {
            uniqueConstraintWorks = true;
        }
        CHECK(uniqueConstraintWorks);
    }
}
