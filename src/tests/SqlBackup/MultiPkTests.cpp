// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <iostream>

using namespace Lightweight;

namespace
{

std::filesystem::path const SqliteDatabasePath = "multipk_test.db";
SqlConnectionString const ConnectionString { "DRIVER=SQLite3;Database=" + SqliteDatabasePath.string() };
std::filesystem::path const BackupFile = "multipk_test.zip";

struct ScopedFileRemoved
{
    std::filesystem::path path;

    void RemoveIfExists() const
    {
        if (std::filesystem::exists(path))
            std::filesystem::remove(path);
    }

    ScopedFileRemoved(std::filesystem::path path):
        path(std::move(path))
    {
        RemoveIfExists();
    }

    ~ScopedFileRemoved()
    {
        RemoveIfExists();
    }

    ScopedFileRemoved(ScopedFileRemoved const&) = delete;
    ScopedFileRemoved& operator=(ScopedFileRemoved const&) = delete;
    ScopedFileRemoved(ScopedFileRemoved&&) = delete;
    ScopedFileRemoved& operator=(ScopedFileRemoved&&) = delete;
};

struct SilentProgressManager: SqlBackup::ProgressManager
{
    void Update(SqlBackup::Progress const& p) override
    {
        if (p.state == SqlBackup::Progress::State::Error)
            std::cerr << "Error: " << p.message << "\n";
    }
    void AllDone() override {}
};

} // namespace

TEST_CASE("SqlBackup: Composite Primary Key Order", "[SqlBackup][MultiPk]")
{
    ScopedFileRemoved const backupFileCleaner { BackupFile };

    // 1. Setup database with composite PK in specific order (b, a)
    {
        SqlConnection conn;
        conn.Connect(SqlConnection::DefaultConnectionString());
        SqlStatement stmt { conn };
        stmt.ExecuteDirect("DROP TABLE IF EXISTS multipk");
        stmt.ExecuteDirect("CREATE TABLE multipk (a INTEGER, b INTEGER, content VARCHAR(100), PRIMARY KEY (b, a))");
        stmt.ExecuteDirect("INSERT INTO multipk (a, b, content) VALUES (1, 2, 'row1')");
    }

    // 2. Backup
    {
        SilentProgressManager pm;
        SqlBackup::Backup(BackupFile, SqlConnection::DefaultConnectionString(), 1, pm);
    }

    // 3. Restore to a fresh state
    {
        SqlConnection conn;
        conn.Connect(SqlConnection::DefaultConnectionString());
        SqlStatement { conn }.ExecuteDirect("DROP TABLE multipk");

        SilentProgressManager pm;
        SqlBackup::Restore(BackupFile, SqlConnection::DefaultConnectionString(), 1, pm);
    }

    // 4. Verify PK order in the restored table
    {
        SqlConnection conn;
        conn.Connect(SqlConnection::DefaultConnectionString());
        SqlStatement stmt { conn };

        std::vector<std::string> pkColumns;

        if (conn.ServerType() == SqlServerType::MICROSOFT_SQL)
        {
            stmt.ExecuteDirect(R"(
                SELECT COLUMN_NAME
                FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE
                WHERE OBJECTPROPERTY(OBJECT_ID(CONSTRAINT_SCHEMA + '.' + CONSTRAINT_NAME), 'IsPrimaryKey') = 1
                  AND TABLE_NAME = 'multipk'
                ORDER BY ORDINAL_POSITION
            )");
            while (stmt.FetchRow())
            {
                pkColumns.push_back(stmt.GetColumn<std::string>(1));
            }
        }
        else if (conn.ServerType() == SqlServerType::POSTGRESQL)
        {
            // PostgreSQL: Query primary key columns from information_schema
            stmt.ExecuteDirect(R"(
                SELECT kcu.column_name
                FROM information_schema.table_constraints tc
                JOIN information_schema.key_column_usage kcu
                    ON tc.constraint_name = kcu.constraint_name
                    AND tc.table_schema = kcu.table_schema
                WHERE tc.constraint_type = 'PRIMARY KEY'
                    AND tc.table_name = 'multipk'
                ORDER BY kcu.ordinal_position
            )");
            while (stmt.FetchRow())
            {
                pkColumns.push_back(stmt.GetColumn<std::string>(1));
            }
        }
        else
        {
            // SQLite
            // PRAGMA table_info returns columns.
            stmt.ExecuteDirect("PRAGMA table_info(multipk)");
            std::vector<std::pair<int, std::string>> explicitPks;
            while (stmt.FetchRow())
            {
                auto name = stmt.GetColumn<std::string>(2);
                auto pk = stmt.GetColumn<int>(6);
                if (pk > 0)
                {
                    explicitPks.emplace_back(pk, name);
                }
            }
            std::ranges::sort(explicitPks);
            for (auto const& p: explicitPks)
                pkColumns.push_back(p.second);
        }

        REQUIRE(pkColumns.size() == 2);
        // We expect (b, a) order.
        CHECK(pkColumns[0] == "b");
        CHECK(pkColumns[1] == "a");
    }
}
