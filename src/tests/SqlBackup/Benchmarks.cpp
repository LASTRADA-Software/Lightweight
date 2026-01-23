// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <iostream>
#include <random>

using namespace Lightweight;

namespace
{

struct LambdaProgressManager: SqlBackup::ProgressManager
{
    void Update(SqlBackup::Progress const&) override {}
    void AllDone() override {}
};

std::filesystem::path const BackupFile = "benchmark_test.zip";

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

void SetupBenchmarkDatabase(size_t rows)
{
    SqlConnection conn;
    conn.Connect(SqlConnection::DefaultConnectionString());
    SqlStatement stmt { conn };

    stmt.ExecuteDirect("DROP TABLE IF EXISTS bench_table");

    // Create a table with mix of types to stress packed formats
    std::string const idType = conn.ServerType() == SqlServerType::MICROSOFT_SQL ? "INT IDENTITY(1,1) PRIMARY KEY" : "INTEGER PRIMARY KEY";
    stmt.ExecuteDirect(std::format(R"(
        CREATE TABLE bench_table (
            id {},
            val_int INTEGER,
            val_real REAL,
            val_text VARCHAR(100)
        )
    )", idType));

    stmt.ExecuteDirect("BEGIN TRANSACTION");
    stmt.Prepare("INSERT INTO bench_table (val_int, val_real, val_text) VALUES (?, ?, ?)");

    std::mt19937 gen(42); // fixed seed
    std::uniform_int_distribution<int64_t> distInt(0, 1000000);
    std::uniform_real_distribution<double> distReal(0.0, 1000000.0);

    for (size_t i = 0; i < rows; ++i)
    {
        stmt.Execute(distInt(gen), distReal(gen), std::format("Row {}", i));
    }
    stmt.ExecuteDirect("COMMIT");
}

} // namespace

TEST_CASE("SqlBackup Performance", "[.][Benchmark]")
{
    ScopedFileRemoved const backupFileCleaner { BackupFile };

    // Setup 100k rows for benchmark
    size_t const RowCount = 100000;
    SetupBenchmarkDatabase(RowCount);

    LambdaProgressManager pm;

    BENCHMARK("Backup 100k rows")
    {
        // We delete the backup file each time to ensure write
        if (std::filesystem::exists(BackupFile))
            std::filesystem::remove(BackupFile);
        SqlBackup::Backup(BackupFile, SqlConnection::DefaultConnectionString(), 1, pm);
    };

    // Prepare for restore benchmark
    if (std::filesystem::exists(BackupFile))
        std::filesystem::remove(BackupFile);
    SqlBackup::Backup(BackupFile, SqlConnection::DefaultConnectionString(), 1, pm);

    BENCHMARK("Restore 100k rows")
    {
        // Drop table first
        {
            SqlConnection conn;
            conn.Connect(SqlConnection::DefaultConnectionString());
            SqlStatement { conn }.ExecuteDirect("DROP TABLE bench_table");
        }
        SqlBackup::Restore(BackupFile, SqlConnection::DefaultConnectionString(), 1, pm);
    };
}
