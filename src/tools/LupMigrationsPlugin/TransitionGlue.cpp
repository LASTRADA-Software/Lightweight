// SPDX-License-Identifier: Apache-2.0

#include "TransitionGlue.hpp"

#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlErrorDetection.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <print>

namespace Lup
{

namespace
{
    /// Timestamp prefix used by `lup2dbtool`-generated migrations.
    /// Mirrors `Lup2DbTool::LupVersion::ToMigrationTimestamp` — keep in sync.
    constexpr uint64_t kTimestampPrefix = 20'000'000'000'000ULL;

    /// Decode a LUP version integer (e.g. 60912 → 6.9.12) into a dotted string
    /// for human-readable banner output.
    [[nodiscard]] std::string FormatLupVersion(int64_t versionInteger) noexcept
    {
        // Encoding mirrors `Lup2DbTool::LupVersion::ToInteger`:
        //   pre-6.0.0:  major * 100   + minor * 10  + patch
        //   >= 6.0.0:   major * 10000 + minor * 100 + patch
        // Anything >= 60000 must be the post-6.0.0 encoding.
        if (versionInteger >= 60'000)
        {
            auto const major = versionInteger / 10'000;
            auto const minor = (versionInteger / 100) % 100;
            auto const patch = versionInteger % 100;
            return std::format("{}.{}.{}", major, minor, patch);
        }
        auto const major = versionInteger / 100;
        auto const minor = (versionInteger / 10) % 10;
        auto const patch = versionInteger % 10;
        return std::format("{}.{}.{}", major, minor, patch);
    }
} // namespace

std::optional<int64_t> TransitionGlue::GetCurrentLupVersion(Lightweight::SqlConnection& connection)
{
    // The legacy LUpd installer stores its version in LASTRADA_PROPERTIES (NR=4, VALUE=<encoded int>).
    // Probe the table; if it doesn't exist this is either a fresh modern database
    // or a non-LUpd-managed schema — both legitimately yield nullopt. Anything else
    // (driver failure, permission denied, malformed row) should surface so the
    // operator notices instead of silently falling back to "no transition needed".
    try
    {
        Lightweight::SqlStatement stmt(connection);
        auto cursor = stmt.ExecuteDirect("SELECT VALUE FROM LASTRADA_PROPERTIES WHERE NR = 4");
        if (cursor.FetchRow())
            return cursor.GetColumn<int64_t>(1);
        return std::nullopt;
    }
    catch (Lightweight::SqlException const& ex)
    {
        if (Lightweight::IsTableNotFoundError(ex.info(), connection.ServerType()))
            return std::nullopt;
        throw;
    }
}

size_t TransitionGlue::MarkMigrationsAsApplied(Lightweight::SqlMigration::MigrationManager& manager,
                                               int64_t maxVersionInteger)
{
    auto const maxTimestamp = VersionToTimestamp(maxVersionInteger);

    manager.CreateMigrationHistory();

    auto const& allMigrations = manager.GetAllMigrations();
    auto const appliedIds = manager.GetAppliedMigrationIds();

    size_t markedCount = 0;
    for (auto const* migration: allMigrations)
    {
        auto const timestamp = migration->GetTimestamp();
        if (timestamp.value > maxTimestamp)
            continue;

        if (std::ranges::find(appliedIds, timestamp) != appliedIds.end())
            continue;

        manager.MarkMigrationAsApplied(*migration);
        ++markedCount;
    }

    return markedCount;
}

bool TransitionGlue::Initialize(Lightweight::SqlMigration::MigrationManager& manager, Lightweight::SqlConnection& connection)
{
    // One-shot: once `schema_migrations` has any applied row, this database
    // has either already been transitioned or was bootstrapped under the
    // modern flow. Either way, we have no business writing more rows here —
    // bail out cheaply so repeated `dbtool status` invocations stay free of
    // catalog probes for the LUpd legacy table.
    if (!manager.GetAppliedMigrationIds().empty())
        return true;

    auto const currentVersion = GetCurrentLupVersion(connection);
    if (!currentVersion.has_value())
        return true; // Fresh / non-LUpd database — nothing to transition.

    auto const markedCount = MarkMigrationsAsApplied(manager, *currentVersion);
    if (markedCount > 0)
    {
        // Surface the transition so operators see what happened on first run.
        // The banner goes to stdout because it is informational status, not an error.
        std::println("Transitioning from LASTRADA_PROPERTIES (LUP version {}): "
                     "marked {} migration(s) as applied in schema_migrations.",
                     FormatLupVersion(*currentVersion),
                     markedCount);
    }

    return true;
}

uint64_t TransitionGlue::VersionToTimestamp(int64_t versionInteger)
{
    // LUP version integers are directly used as the lower part of the timestamp
    // (e.g. 60808 -> 20000000060808). Mirrors `LupVersion::ToMigrationTimestamp`.
    return kTimestampPrefix + static_cast<uint64_t>(versionInteger);
}

} // namespace Lup
