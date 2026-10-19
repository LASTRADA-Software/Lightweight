// SPDX-License-Identifier: Apache-2.0

#include "../../tools/dbtool/BackupRequirementReport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>

using namespace Lightweight;
using namespace Lightweight::Tools;
using SqlMigration::BackupRecord;
using SqlMigration::BackupRequirement;

TEST_CASE("JoinReasons lists one, two and three reasons", "[dbtool][BackupRequirement]")
{
    CHECK(JoinReasons({ "release 3.0.0" }) == "release 3.0.0");
    CHECK(JoinReasons({ "release 3.0.0", "step \"X\"" }) == "release 3.0.0 and step \"X\"");
    CHECK(JoinReasons({ "a", "b", "c" }) == "a, b and c");
}

TEST_CASE("The refusal names the reasons and how to take a backup", "[dbtool][BackupRequirement]")
{
    auto const requirement = BackupRequirement {
        .required = true, .reasons = { "release 3.0.0" }, .satisfied = false, .lastBackup = std::nullopt
    };
    CHECK(FormatBackupRequirementRefusal(requirement, "migrate", {})
          == "release 3.0.0 requires a backup of this database taken after its last migration.\n"
             "Run: dbtool backup --output <file>   or   dbtool migrate --backup-to <file>\n"
             "Or, to migrate without a backup (recorded as waived): dbtool migrate --without-backup");
}

TEST_CASE("The refusal says why the newest backup does not count", "[dbtool][BackupRequirement]")
{
    auto const createdAt =
        SqlDateTime { std::chrono::sys_days { std::chrono::year { 2026 } / 10 / 18 } + std::chrono::hours { 9 } };
    auto const requirement = BackupRequirement {
        .required = true,
        .reasons = { "release 3.0.0", "step \"Recorder step\"" },
        .satisfied = false,
        .lastBackup = BackupRecord { .id = 1,
                                     .createdAt = createdAt,
                                     .archiveFileName = "Sicherung-M\xC3\xBCller.zip",
                                     .archiveSha256 = {},
                                     .observedState = {},
                                     .toolVersion = {},
                                     .kind = {} },
    };
    CHECK(FormatBackupRequirementRefusal(
              requirement, "migrate-to-release 3.0.0", "The backup could not be recorded in schema_backups (denied).")
          == "release 3.0.0 and step \"Recorder step\" require a backup of this database taken after its last migration.\n"
             "The newest backup (Sicherung-M\xC3\xBCller.zip, 2026-10-18 09:00:00 UTC) does not match the current migration "
             "history.\n"
             "The backup could not be recorded in schema_backups (denied).\n"
             "Run: dbtool backup --output <file>   or   dbtool migrate-to-release 3.0.0 --backup-to <file>\n"
             "Or, to migrate without a backup (recorded as waived): dbtool migrate-to-release 3.0.0 --without-backup");
}

TEST_CASE("A waived run names what it skips", "[dbtool][BackupRequirement]")
{
    auto const requirement = BackupRequirement { .required = true,
                                                 .reasons = { "migration 20240201000000 \"Add index\"" },
                                                 .satisfied = false,
                                                 .lastBackup = std::nullopt };
    CHECK(
        FormatBackupWaiver(requirement, {})
        == "Migrating without a backup although migration 20240201000000 \"Add index\" requires one; recorded as waived in "
           "schema_backups.");
    // A row that could not be written is not called recorded.
    CHECK(FormatBackupWaiver(requirement, "denied")
          == "Migrating without a backup although migration 20240201000000 \"Add index\" requires one; the waiver could "
             "not be recorded in schema_backups (denied).");
}

TEST_CASE("The dry run reports a required backup on one line", "[dbtool][BackupRequirement]")
{
    auto missing = BackupRequirement {
        .required = true, .reasons = { "release 3.0.0" }, .satisfied = false, .lastBackup = std::nullopt
    };
    auto const prefix = std::string {
        "-- Backup required: release 3.0.0 requires a backup of this database taken after its last migration; "
    };
    CHECK(FormatBackupRequirementDryRun(missing, BackupOverride::None) == prefix + "none covers it, so a real run refuses.");
    CHECK(FormatBackupRequirementDryRun(missing, BackupOverride::BackupFirst)
          == prefix + "none covers it yet, so a real run backs up first (--backup-to).");
    CHECK(FormatBackupRequirementDryRun(missing, BackupOverride::Waive)
          == prefix + "none covers it, so a real run migrates without one and records the waiver (--without-backup).");
    missing.satisfied = true;
    CHECK(FormatBackupRequirementDryRun(missing, BackupOverride::None) == prefix + "the newest backup covers it.");
}
