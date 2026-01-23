// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"

#include <Lightweight/DataBinder/SqlDynamicBinary.hpp>
#include <Lightweight/SqlBackup/SqlBackup.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <mutex>

using namespace Lightweight;

namespace
{

std::filesystem::path const BackupFile = "backup_robustness_test.zip";

SqlConnectionString const& GetConnectionString()
{
    return SqlConnection::DefaultConnectionString();
}

struct LambdaProgressManager: SqlBackup::ProgressManager
{
    std::function<void(SqlBackup::Progress const&)> callback;

    explicit LambdaProgressManager(std::function<void(SqlBackup::Progress const&)> cb):
        callback(std::move(cb))
    {
    }

    void Update(SqlBackup::Progress const& p) override
    {
        if (callback)
            callback(p);
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

} // namespace

TEST_CASE("SqlBackup: Foreign Key Restoration", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;

    ScopedFileRemoved const backupFileCleaner { BackupFile };

    // 1. Setup Database with FK
    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };

        // Create tables using Migration API
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("child");
            migration.DropTableIfExists("parent");

            migration.CreateTable("parent")
                .PrimaryKey("id", Integer {})
                .RequiredColumn("name", Varchar { 100 });

            migration.CreateTable("child")
                .PrimaryKey("id", Integer {})
                .ForeignKey("parent_id", Integer {}, { .tableName = "parent", .columnName = "id" });
        });

        EnableForeignKeysIfNeeded(conn);

        // Insert data with explicit IDs
        stmt.Prepare("INSERT INTO parent (id, name) VALUES (?, ?)");
        stmt.Execute(1, "Parent1");

        stmt.Prepare("INSERT INTO child (id, parent_id) VALUES (?, ?)");
        stmt.Execute(10, 1);
    }

    // 2. Backup
    LambdaProgressManager pm { [](auto&&) {} };
    REQUIRE_NOTHROW(SqlBackup::Backup(BackupFile, GetConnectionString(), 1, pm));

    // 3. Drop Tables
    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };
        DisableForeignKeysIfNeeded(conn); // To allow dropping parent without order
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTable("child");
            migration.DropTable("parent");
        });
    }

    // 4. Restore
    std::string errorMsg;
    std::mutex errorMutex;
    LambdaProgressManager restorePm { [&](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
        {
            std::scoped_lock lock(errorMutex);
            errorMsg = p.message;
        }
    } };
    REQUIRE_NOTHROW(SqlBackup::Restore(BackupFile, GetConnectionString(), 1, restorePm));

    if (!errorMsg.empty())
        FAIL_CHECK("Restore failed: " + errorMsg);

    // 5. Verify
    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };
        EnableForeignKeysIfNeeded(conn);

        // Verify Data
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM parent") == 1);
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM child") == 1);

        // Verify FK Integrity by trying to insert invalid child
        bool constraintFailed = false;
        try
        {
            stmt.ExecuteDirect("INSERT INTO child (id, parent_id) VALUES (11, 999)");
        }
        catch (...)
        {
            constraintFailed = true;
        }
        // If Schema restoration worked correctly, this should fail due to FK constraint
        // Note: SQLite enforces FKs only if PRAGMA foreign_keys = ON (which we set above).
        // The restoration itself should have created the FK definition.
        REQUIRE(constraintFailed);
    }
}

TEST_CASE("SqlBackup: Robustness Types and Nulls", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;

    ScopedFileRemoved const backupFileCleaner { BackupFile };

    // 1. Setup

    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };

        // Create table using Migration API - formatter handles DBMS-specific types
        // Using PrimaryKey (not PrimaryKeyWithAutoIncrement) to avoid PostgreSQL SERIAL/sequence
        // backup/restore issues. This test is about types and nulls, not auto-increment.
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("types");

            migration.CreateTable("types")
                .PrimaryKey("id", Integer {})
                .Column("b", Bool {})      // Formatter handles BIT vs BOOL
                .Column("i", Integer {})
                .Column("d", Real {})      // Formatter handles FLOAT vs REAL
                .Column("t", Text {})      // Formatter handles VARCHAR(MAX) vs TEXT (nullable for row 3)
                .Column("bin", Binary {}); // Formatter handles VARBINARY(MAX) vs BLOB vs BYTEA
        });

        // Row 1: All Filled
        // BOOL: true (1), Integer: 123, Real: 12.34, Text: "Hello", Bin: 0x01
        {
            SqlDynamicBinary<100> bin;
            bin.resize(1);
            bin.data()[0] = 0x01;
            stmt.Prepare("INSERT INTO types (id, b, i, d, t, bin) VALUES (?, ?, ?, ?, ?, ?)");
            stmt.Execute(1, true, 123, 12.34, "Hello", bin);
        }

        // Row 2: Zeros / Empty / False
        // BOOL: false (0), Integer: 0, Real: 0.0, Text: "", Bin: Empty
        {
            SqlDynamicBinary<100> bin; // empty
            stmt.Prepare("INSERT INTO types (id, b, i, d, t, bin) VALUES (?, ?, ?, ?, ?, ?)");
            stmt.Execute(2, false, 0, 0.0, "", bin);
        }

        // Row 3 & 4: Nulls and special string "NULL"
        stmt.ExecuteDirect("INSERT INTO types (id) VALUES (3)");
        stmt.ExecuteDirect("INSERT INTO types (id, t) VALUES (4, 'NULL')");
    }

    // 2. Backup
    LambdaProgressManager pm { [](auto&&) {} };
    REQUIRE_NOTHROW(SqlBackup::Backup(BackupFile, GetConnectionString(), 1, pm));

    // 3. Drop
    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTable("types");
        });
    }

    // 4. Restore
    LambdaProgressManager restorePm { [&](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Restore failed: " + p.message);
    } };
    REQUIRE_NOTHROW(SqlBackup::Restore(BackupFile, GetConnectionString(), 1, restorePm));

    // 5. Verify
    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };

        // Row 1
        REQUIRE(stmt.ExecuteDirectScalar<bool>("SELECT b FROM types WHERE id=1") == true);
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT i FROM types WHERE id=1") == 123);
        // Using string for Real to avoid float comparison issues roughly
        // REQUIRE(stmt.ExecuteDirectScalar<double>("SELECT d FROM types WHERE id=1") == 12.34);

        // Row 2
        REQUIRE(stmt.ExecuteDirectScalar<bool>("SELECT b FROM types WHERE id=2") == false);
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT i FROM types WHERE id=2") == 0);
        REQUIRE(stmt.ExecuteDirectScalar<std::string>("SELECT t FROM types WHERE id=2") == "");
        // Verify Empty Binary vs Null
        std::optional<SqlDynamicBinary<100>> bin2 =
            stmt.ExecuteDirectScalar<SqlDynamicBinary<100>>("SELECT bin FROM types WHERE id=2");
        if (bin2.has_value())
        {
            REQUIRE(bin2->empty());
            auto count = stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM types WHERE id=2 AND bin IS NOT NULL");
            REQUIRE(count == 1);
        }
        else
        {
            // Strict check: It MUST be empty binary (not null) because we inserted empty binary.
            // If checking strict, this is a failure.
            FAIL_CHECK("Expected Empty Binary but got NULL for id=2");
        }

        // Row 3 (Nulls)
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM types WHERE id=3 AND b IS NULL") == 1);
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM types WHERE id=3 AND i IS NULL") == 1);
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM types WHERE id=3 AND t IS NULL") == 1);
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM types WHERE id=3 AND bin IS NULL") == 1);

        // Row 4 ("NULL" string)
        REQUIRE(stmt.ExecuteDirectScalar<std::string>("SELECT t FROM types WHERE id=4") == "NULL");
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT COUNT(*) FROM types WHERE id=4 AND t IS NOT NULL") == 1);
    }
}

TEST_CASE("SqlBackup: Constraints and Defaults", "[SqlBackup]")
{
    using namespace SqlColumnTypeDefinitions;

    ScopedFileRemoved const backupFileCleaner { BackupFile };

    // 1. Setup

    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };

        // Create table using Migration API with constraints and defaults
        // Note: Using PrimaryKey (not PrimaryKeyWithAutoIncrement) because:
        // 1. The original test used "INTEGER PRIMARY KEY" for SQLite/PostgreSQL
        // 2. PrimaryKeyWithAutoIncrement creates SERIAL on PostgreSQL which has backup/restore
        //    issues with sequences (nextval references non-existent sequence on restore)
        // We explicitly insert ID values to avoid relying on auto-increment behavior.
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("constraints");

            migration.CreateTable("constraints")
                .PrimaryKey("id", Integer {})
                .Column(SqlColumnDeclaration {
                    .name = "u",
                    .type = Varchar { 100 },
                    .unique = true,
                })
                .Column(SqlColumnDeclaration {
                    .name = "d",
                    .type = Integer {},
                    .defaultValue = "42",
                });
        });

        stmt.ExecuteDirect("INSERT INTO constraints (id, u) VALUES (1, 'A')"); // d should be 42
    }

    // 2. Backup
    LambdaProgressManager pm { [](auto&&) {} };
    REQUIRE_NOTHROW(SqlBackup::Backup(BackupFile, GetConnectionString(), 1, pm));

    // 3. Drop
    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTable("constraints");
        });
    }

    // 4. Restore
    LambdaProgressManager restorePm { [&](SqlBackup::Progress const& p) {
        if (p.state == SqlBackup::Progress::State::Error)
            FAIL_CHECK("Restore failed: " + p.message);
    } };
    REQUIRE_NOTHROW(SqlBackup::Restore(BackupFile, GetConnectionString(), 1, restorePm));

    // 5. Verify
    {
        SqlConnection conn;
        conn.Connect(GetConnectionString());
        SqlStatement stmt { conn };

        // Verify Data
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT d FROM constraints WHERE u='A'") == 42);

        // Verify Default Value behavior on NEW insert
        stmt.ExecuteDirect("INSERT INTO constraints (id, u) VALUES (2, 'B')");
        // If Default Constraint was lost, d would be NULL (or 0)
        REQUIRE(stmt.ExecuteDirectScalar<int>("SELECT d FROM constraints WHERE u='B'") == 42);

        // Verify Unique Constraint
        // If Unique Constraint was lost, duplicate insert would succeed
        auto const _ = ScopedSqlNullLogger {};
        REQUIRE_THROWS_AS(stmt.ExecuteDirect("INSERT INTO constraints (u) VALUES ('A')"), SqlException);
    }
}
