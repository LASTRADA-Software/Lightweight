// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <nlohmann/json.hpp>

using namespace Lightweight;

namespace
{

std::filesystem::path const BackupFile = "production_readiness_test.zip";

SqlConnectionString const& GetConnectionString()
{
    return SqlConnection::DefaultConnectionString();
}

struct ErrorCountingProgressManager: SqlBackup::ErrorTrackingProgressManager
{
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void Update(SqlBackup::Progress const& p) override
    {
        ErrorTrackingProgressManager::Update(p);
        if (p.state == SqlBackup::Progress::State::Error)
            errors.push_back(p.message);
        else if (p.state == SqlBackup::Progress::State::Warning)
            warnings.push_back(p.message);
    }

    void AllDone() override {}
};

struct ScopedFileRemoved
{
    std::filesystem::path path;

    void RemoveIfExists() const
    {
        if (std::filesystem::exists(path))
            std::filesystem::remove(path);
    }

    explicit ScopedFileRemoved(std::filesystem::path p):
        path(std::move(p))
    {
        RemoveIfExists();
    }

    ~ScopedFileRemoved() { RemoveIfExists(); }

    ScopedFileRemoved(ScopedFileRemoved const&) = delete;
    ScopedFileRemoved& operator=(ScopedFileRemoved const&) = delete;
    ScopedFileRemoved(ScopedFileRemoved&&) = delete;
    ScopedFileRemoved& operator=(ScopedFileRemoved&&) = delete;
};

void SetupTestTable()
{
    using namespace SqlColumnTypeDefinitions;

    SqlConnection conn;
    conn.Connect(GetConnectionString());
    SqlStatement stmt { conn };

    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.DropTableIfExists("prod_test_table");
        migration.CreateTable("prod_test_table").PrimaryKey("id", Integer {}).Column("name", Varchar { 100 });
    });

    stmt.Prepare("INSERT INTO prod_test_table (id, name) VALUES (?, ?)");
    stmt.Execute(1, "Alice");
    stmt.Execute(2, "Bob");
    stmt.Execute(3, "Charlie");
}

} // namespace

TEST_CASE("SqlBackup: Error counting in ProgressManager", "[SqlBackup][ProductionReadiness]")
{
    ScopedFileRemoved const backupFileCleaner { BackupFile };

    ErrorCountingProgressManager pm;

    // Test that error count starts at 0
    REQUIRE(pm.ErrorCount() == 0);

    // Simulate an error
    pm.Update({ .state = SqlBackup::Progress::State::Error,
                .tableName = "test",
                .currentRows = 0,
                .totalRows = 0,
                .message = "Test error" });

    REQUIRE(pm.ErrorCount() == 1);
    REQUIRE(pm.errors.size() == 1);

    // Simulate another error
    pm.Update({ .state = SqlBackup::Progress::State::Error,
                .tableName = "test2",
                .currentRows = 0,
                .totalRows = 0,
                .message = "Another error" });

    REQUIRE(pm.ErrorCount() == 2);

    // Non-error states should not increment
    pm.Update({ .state = SqlBackup::Progress::State::InProgress,
                .tableName = "test",
                .currentRows = 50,
                .totalRows = 100,
                .message = "" });

    REQUIRE(pm.ErrorCount() == 2);
}

TEST_CASE("SqlBackup: Format version in metadata", "[SqlBackup][ProductionReadiness]")
{
    ScopedFileRemoved const backupFileCleaner { BackupFile };
    SetupTestTable();

    // Create backup
    ErrorCountingProgressManager pm;
    SqlBackup::Backup(BackupFile, GetConnectionString(), 1, pm);
    REQUIRE(pm.ErrorCount() == 0);

    // Read and verify metadata contains format_version
    int err = 0;
    zip_t* zip = zip_open(BackupFile.string().c_str(), ZIP_RDONLY, &err);
    REQUIRE(zip != nullptr);

    zip_int64_t const metadataIndex = zip_name_locate(zip, "metadata.json", 0);
    REQUIRE(metadataIndex >= 0);

    zip_stat_t metaStat;
    zip_stat_index(zip, metadataIndex, 0, &metaStat);

    zip_file_t* file = zip_fopen_index(zip, metadataIndex, 0);
    std::string metadataStr(metaStat.size, '\0');
    zip_fread(file, metadataStr.data(), metaStat.size);
    zip_fclose(file);
    zip_close(zip);

    nlohmann::json const metadata = nlohmann::json::parse(metadataStr);
    REQUIRE(metadata.contains("format_version"));
    REQUIRE(metadata["format_version"] == "1.0");
}

TEST_CASE("SqlBackup: Checksums in backup", "[SqlBackup][ProductionReadiness]")
{
    ScopedFileRemoved const backupFileCleaner { BackupFile };
    SetupTestTable();

    // Create backup
    ErrorCountingProgressManager pm;
    SqlBackup::Backup(BackupFile, GetConnectionString(), 1, pm);
    REQUIRE(pm.ErrorCount() == 0);

    // Verify checksums.json exists
    int err = 0;
    zip_t* zip = zip_open(BackupFile.string().c_str(), ZIP_RDONLY, &err);
    REQUIRE(zip != nullptr);

    zip_int64_t const checksumIndex = zip_name_locate(zip, "checksums.json", 0);
    REQUIRE(checksumIndex >= 0);

    zip_stat_t checksumStat;
    zip_stat_index(zip, checksumIndex, 0, &checksumStat);

    zip_file_t* file = zip_fopen_index(zip, checksumIndex, 0);
    std::string checksumStr(checksumStat.size, '\0');
    zip_fread(file, checksumStr.data(), checksumStat.size);
    zip_fclose(file);
    zip_close(zip);

    nlohmann::json const checksums = nlohmann::json::parse(checksumStr);
    REQUIRE(checksums.contains("algorithm"));
    REQUIRE(checksums["algorithm"] == "sha256");
    REQUIRE(checksums.contains("files"));
    REQUIRE(checksums["files"].is_object());

    // Should have at least one checksum entry
    REQUIRE(!checksums["files"].empty());
}

TEST_CASE("SqlBackup: Filter tables in restore", "[SqlBackup][ProductionReadiness]")
{
    using namespace SqlColumnTypeDefinitions;

    ScopedFileRemoved const backupFileCleaner { BackupFile };

    // Setup multiple tables
    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };

        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("users_table");
            migration.DropTableIfExists("products_table");
            migration.DropTableIfExists("orders_table");

            migration.CreateTable("users_table").PrimaryKey("id", Integer {}).Column("name", Varchar { 100 });
            migration.CreateTable("products_table").PrimaryKey("id", Integer {}).Column("name", Varchar { 100 });
            migration.CreateTable("orders_table").PrimaryKey("id", Integer {}).Column("amount", Integer {});
        });

        stmt.ExecuteDirect("INSERT INTO users_table (id, name) VALUES (1, 'User1')");
        stmt.ExecuteDirect("INSERT INTO products_table (id, name) VALUES (1, 'Product1')");
        stmt.ExecuteDirect("INSERT INTO orders_table (id, amount) VALUES (1, 100)");
    }

    // Backup all tables
    ErrorCountingProgressManager backupPm;
    SqlBackup::Backup(BackupFile, GetConnectionString(), 1, backupPm);
    REQUIRE(backupPm.ErrorCount() == 0);

    // Drop all tables
    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("users_table");
            migration.DropTableIfExists("products_table");
            migration.DropTableIfExists("orders_table");
        });
    }

    // Restore only users_table
    ErrorCountingProgressManager restorePm;
    SqlBackup::Restore(BackupFile, GetConnectionString(), 1, restorePm, "", "users_table");
    REQUIRE(restorePm.ErrorCount() == 0);

    // Verify only users_table was restored
    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };

        // users_table should exist and have data
        auto userCount = stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM users_table");
        REQUIRE(userCount == 1);

        // products_table and orders_table should not exist
        // (attempting to query them would throw, so we check via schema)
        auto tables = SqlSchema::ReadAllTables(stmt, conn.DatabaseName(), "", nullptr);
        bool hasProducts = false;
        bool hasOrders = false;
        for (auto const& t: tables)
        {
            if (t.name == "products_table")
                hasProducts = true;
            if (t.name == "orders_table")
                hasOrders = true;
        }
        REQUIRE_FALSE(hasProducts);
        REQUIRE_FALSE(hasOrders);
    }
}

TEST_CASE("SqlBackup: TableFilter patterns", "[SqlBackup][ProductionReadiness]")
{
    auto const filter1 = SqlBackup::TableFilter::Parse("*");
    REQUIRE(filter1.MatchesAll());

    auto const filter2 = SqlBackup::TableFilter::Parse("Users,Products");
    REQUIRE_FALSE(filter2.MatchesAll());
    REQUIRE(filter2.Matches("", "Users"));
    REQUIRE(filter2.Matches("", "Products"));
    REQUIRE_FALSE(filter2.Matches("", "Orders"));

    auto const filter3 = SqlBackup::TableFilter::Parse("*_log");
    REQUIRE(filter3.Matches("", "audit_log"));
    REQUIRE(filter3.Matches("", "error_log"));
    REQUIRE_FALSE(filter3.Matches("", "users"));

    auto const filter4 = SqlBackup::TableFilter::Parse("User*");
    REQUIRE(filter4.Matches("", "Users"));
    REQUIRE(filter4.Matches("", "UserAccounts"));
    REQUIRE_FALSE(filter4.Matches("", "Products"));
}
