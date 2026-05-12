// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string_view>
#include <tuple>

#include <TransitionGlue.hpp>

using namespace Lightweight;

namespace
{

/// Fixture for `Lup::TransitionGlue` tests.
///
/// Inherits `SqlTestFixture` so every test runs against whichever DBMS is
/// selected by `--test-env=<name>` (sqlite3, mssql2022, postgres, …). The base
/// fixture's constructor drops every table in the database, which transparently
/// cleans up the auxiliary `LASTRADA_PROPERTIES` table left behind by previous
/// tests — no DBMS-specific teardown is needed here.
class TransitionGlueTestFixture: public SqlTestFixture
{
  public:
    TransitionGlueTestFixture():
        SqlTestFixture()
    {
        auto& manager = SqlMigration::MigrationManager::GetInstance();
        manager.RemoveAllMigrations();
        manager.RemoveAllReleases();
    }

    TransitionGlueTestFixture(TransitionGlueTestFixture&&) = delete;
    TransitionGlueTestFixture(TransitionGlueTestFixture const&) = delete;
    TransitionGlueTestFixture& operator=(TransitionGlueTestFixture&&) = delete;
    TransitionGlueTestFixture& operator=(TransitionGlueTestFixture const&) = delete;

    ~TransitionGlueTestFixture() override
    {
        SqlMigration::MigrationManager::GetInstance().CloseDataMapper();
    }

  protected:
    /// Creates the legacy `LASTRADA_PROPERTIES` table on the given connection and
    /// seeds `NR=4` with the requested LUP version integer. Uses portable column
    /// types (`INTEGER`, `BIGINT`, `VARCHAR`) accepted by all supported DBMSes.
    static void CreateLastradaPropertiesTable(SqlConnection& conn, int64_t lupVersionInteger)
    {
        SqlStatement stmt { conn };
        std::ignore = stmt.ExecuteDirect(
            "CREATE TABLE LASTRADA_PROPERTIES (NR INTEGER PRIMARY KEY, VALUE BIGINT, DESCR VARCHAR(255))");
        std::ignore = stmt.ExecuteDirect(std::format(
            "INSERT INTO LASTRADA_PROPERTIES (NR, VALUE, DESCR) VALUES (4, {}, 'lup_version')", lupVersionInteger));
    }

    [[nodiscard]] static bool IsApplied(SqlMigration::MigrationManager& manager, uint64_t timestamp)
    {
        auto const ids = manager.GetAppliedMigrationIds();
        return std::ranges::any_of(
            ids, [timestamp](SqlMigration::MigrationTimestamp const& id) { return id.value == timestamp; });
    }
};

} // namespace

// All inline migrations share `Migration<TS>` whose timestamp is baked into the
// type. To register a set of four distinct timestamps inline, we declare each
// variable at namespace-or-block scope with its own template instantiation.
//
// Timestamps used across these tests:
//   20'000'000'000'200  →  LUP version 200  (= 2.0.0, pre-6.0.0 encoding)
//   20'000'000'060'000  →  LUP version 60000 (6.0.0)
//   20'000'000'060'808  →  LUP version 60808 (6.8.8)
//   20'000'000'060'912  →  LUP version 60912 (6.9.12, the cutoff)
//   20'000'000'060'913  →  LUP version 60913 (one past the cutoff)

TEST_CASE_METHOD(TransitionGlueTestFixture,
                 "TransitionGlue::Initialize marks pre-6.9.12 migrations applied on every test-env",
                 "[LupTransition]")
{
    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();

    auto& conn = manager.GetDataMapper().Connection();
    CreateLastradaPropertiesTable(conn, 60912);

    // Four migrations that auto-register on construction.
    auto migPre = SqlMigration::Migration<20'000'000'060'000>("v6.0.0", [](SqlMigrationQueryBuilder&) {});
    auto migMid = SqlMigration::Migration<20'000'000'060'808>("v6.8.8", [](SqlMigrationQueryBuilder&) {});
    auto migAtCutoff = SqlMigration::Migration<20'000'000'060'912>("v6.9.12", [](SqlMigrationQueryBuilder&) {});
    auto migPastCutoff = SqlMigration::Migration<20'000'000'060'913>("v6.9.13", [](SqlMigrationQueryBuilder&) {});

    REQUIRE(manager.GetAllMigrations().size() == 4);
    REQUIRE(manager.GetAppliedMigrationIds().empty());

    REQUIRE(Lup::TransitionGlue::Initialize(manager, conn));

    CHECK(IsApplied(manager, 20'000'000'060'000));
    CHECK(IsApplied(manager, 20'000'000'060'808));
    CHECK(IsApplied(manager, 20'000'000'060'912));
    CHECK_FALSE(IsApplied(manager, 20'000'000'060'913));
    CHECK(manager.GetAppliedMigrationIds().size() == 3);
}

TEST_CASE_METHOD(TransitionGlueTestFixture,
                 "TransitionGlue::Initialize is idempotent — second call is a no-op",
                 "[LupTransition]")
{
    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    auto& conn = manager.GetDataMapper().Connection();
    CreateLastradaPropertiesTable(conn, 60912);

    auto mig1 = SqlMigration::Migration<20'000'000'060'000>("v6.0.0", [](SqlMigrationQueryBuilder&) {});
    auto mig2 = SqlMigration::Migration<20'000'000'060'912>("v6.9.12", [](SqlMigrationQueryBuilder&) {});

    REQUIRE(Lup::TransitionGlue::Initialize(manager, conn));
    auto const appliedAfterFirst = manager.GetAppliedMigrationIds().size();
    REQUIRE(appliedAfterFirst == 2);

    // Second invocation must short-circuit and leave applied count unchanged.
    REQUIRE(Lup::TransitionGlue::Initialize(manager, conn));
    CHECK(manager.GetAppliedMigrationIds().size() == appliedAfterFirst);
}

TEST_CASE_METHOD(TransitionGlueTestFixture,
                 "TransitionGlue::Initialize on a fresh modern DB leaves schema_migrations empty",
                 "[LupTransition]")
{
    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    auto& conn = manager.GetDataMapper().Connection();

    // No LASTRADA_PROPERTIES table.
    auto mig = SqlMigration::Migration<20'000'000'060'000>("v6.0.0", [](SqlMigrationQueryBuilder&) {});

    REQUIRE(Lup::TransitionGlue::Initialize(manager, conn));
    CHECK(manager.GetAppliedMigrationIds().empty());
}

TEST_CASE_METHOD(TransitionGlueTestFixture,
                 "TransitionGlue::Initialize short-circuits when schema_migrations already has entries",
                 "[LupTransition]")
{
    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    auto& conn = manager.GetDataMapper().Connection();
    // Both legacy and modern tracking present — the modern table wins, so the
    // legacy probe must never fire.
    CreateLastradaPropertiesTable(conn, 60912);

    auto migA = SqlMigration::Migration<20'000'000'060'000>("v6.0.0", [](SqlMigrationQueryBuilder&) {});
    auto migB = SqlMigration::Migration<20'000'000'060'808>("v6.8.8", [](SqlMigrationQueryBuilder&) {});

    // Simulate "this DB has already been transitioned": mark one migration applied.
    manager.MarkMigrationAsApplied(migA);
    REQUIRE(manager.GetAppliedMigrationIds().size() == 1);

    REQUIRE(Lup::TransitionGlue::Initialize(manager, conn));

    // Early-exit fired: applied count is unchanged (migB stays pending).
    CHECK(manager.GetAppliedMigrationIds().size() == 1);
    CHECK_FALSE(IsApplied(manager, 20'000'000'060'808));
}

TEST_CASE_METHOD(TransitionGlueTestFixture,
                 "TransitionGlue::Initialize handles pre-6.0.0 (low) version encoding",
                 "[LupTransition]")
{
    auto& manager = SqlMigration::MigrationManager::GetInstance();
    manager.CreateMigrationHistory();
    auto& conn = manager.GetDataMapper().Connection();
    // 2.0.0 under the pre-6.0.0 encoding → integer 200.
    CreateLastradaPropertiesTable(conn, 200);

    auto migLow = SqlMigration::Migration<20'000'000'000'200>("v2.0.0", [](SqlMigrationQueryBuilder&) {});
    auto migAbove = SqlMigration::Migration<20'000'000'000'201>("v2.0.1", [](SqlMigrationQueryBuilder&) {});

    REQUIRE(Lup::TransitionGlue::Initialize(manager, conn));

    CHECK(IsApplied(manager, 20'000'000'000'200));
    CHECK_FALSE(IsApplied(manager, 20'000'000'000'201));
}
