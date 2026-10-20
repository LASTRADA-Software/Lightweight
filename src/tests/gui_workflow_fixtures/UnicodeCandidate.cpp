// SPDX-License-Identifier: Apache-2.0
//
// Fixture plugin for the Unicode upgrade: two migrations with wide text columns and a post-init hook
// that registers the pre-migrate step "Widen text columns". With LIGHTWEIGHT_TEST_UNICODE_LEGACY_TYPES
// set, the first migration creates narrow columns instead, so a later run without it finds columns to
// widen on a dialect that tells narrow from wide text. Its file name lacks the `Plugin` suffix, so
// plugin discovery skips it; tests opt in by copying.

#include <Lightweight/ProgressTask.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>

#include <cstdlib>

#if defined(_MSC_VER)
    // C4996 flags getenv as unsafe; the plugin only reads test-harness variables.
    #pragma warning(disable : 4996)
#endif

using namespace Lightweight;

LIGHTWEIGHT_MIGRATION_PLUGIN()

LIGHTWEIGHT_SQL_MIGRATION(20240501000000, "Create unicode_candidate")
{
    using namespace SqlColumnTypeDefinitions;
    auto const legacyTypes = std::getenv("LIGHTWEIGHT_TEST_UNICODE_LEGACY_TYPES") != nullptr;
    auto const nameType = legacyTypes ? SqlColumnTypeDefinition { Varchar(40) } : SqlColumnTypeDefinition { NVarchar(40) };
    auto const codeType = legacyTypes ? SqlColumnTypeDefinition { Char(4) } : SqlColumnTypeDefinition { NChar(4) };
    plan.CreateTable("unicode_candidate").PrimaryKey("id", Integer()).Column("name", nameType).Column("code", codeType);
}

LIGHTWEIGHT_SQL_MIGRATION(20240501000001, "Create unicode_candidate_note")
{
    using namespace SqlColumnTypeDefinitions;
    plan.CreateTable("unicode_candidate_note").PrimaryKey("id", Integer()).Column("note", NVarchar(200));
}

LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT(connection, manager)
{
    static_cast<void>(connection);
    manager.AddPreMigrateStep("Widen text columns",
                              SqlMigration::PreMigrateStepOptions {
                                  .requiresBackup =
                                      [](SqlConnection& stepConnection, SqlMigration::MigrationManager& stepManager) {
                                          return stepConnection.QueryFormatter().DistinguishesNarrowAndWideText()
                                                 && !stepManager
                                                         .UnicodeUpgradeTables(SqlMigration::ExecutionMode::DryRun, nullptr)
                                                         .columns.empty();
                                      },
                              },
                              [](SqlConnection& stepConnection,
                                 SqlMigration::MigrationManager& stepManager,
                                 ProgressTask& task,
                                 SqlMigration::ExecutionMode mode) {
                                  if (stepConnection.QueryFormatter().DistinguishesNarrowAndWideText())
                                      static_cast<void>(stepManager.UnicodeUpgradeTables(mode, &task));
                                  else
                                      task.Log(ProgressTask::LogLevel::Info, "No text columns to upgrade.");
                              });
}
