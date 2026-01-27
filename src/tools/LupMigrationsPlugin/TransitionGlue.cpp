// SPDX-License-Identifier: Apache-2.0

#include "TransitionGlue.hpp"

#include <Lightweight/SqlStatement.hpp>

#include <algorithm>

namespace Lup
{

namespace
{
    // Timestamp prefix for LUP migrations (20000000000000 base)
    constexpr uint64_t kTimestampPrefix = 20000000000000ULL;
} // namespace

std::optional<int64_t> TransitionGlue::GetCurrentLupVersion(Lightweight::SqlConnection& connection)
{
    // Check if LASTRADA_PROPERTIES table exists
    // The table stores: NR (key), VALUE (value), DESCR (description)
    // NR=4 contains the LUP version

    try
    {
        Lightweight::SqlStatement stmt(connection);

        // Try to query the version - if table doesn't exist, this will throw
        stmt.ExecuteDirect("SELECT VALUE FROM LASTRADA_PROPERTIES WHERE NR = 4");
        if (stmt.FetchRow())
            return stmt.GetColumn<int64_t>(1);
    }
    catch (std::exception const& /*ex*/) // NOLINT(bugprone-empty-catch)
    {
        // Table doesn't exist or query failed - this is expected for fresh databases
        // Returning nullopt is the correct behavior here
    }

    return std::nullopt;
}

size_t TransitionGlue::MarkMigrationsAsApplied(Lightweight::SqlMigration::MigrationManager& manager,
                                               int64_t maxVersionInteger)
{
    auto const maxTimestamp = VersionToTimestamp(maxVersionInteger);

    // Ensure migration history table exists
    manager.CreateMigrationHistory();

    // Get all migrations and mark those up to maxTimestamp as applied
    auto const& allMigrations = manager.GetAllMigrations();
    size_t markedCount = 0;

    // Get already applied migrations to avoid duplicates
    auto const appliedIds = manager.GetAppliedMigrationIds();

    for (auto const* migration: allMigrations)
    {
        auto const timestamp = migration->GetTimestamp();
        if (timestamp.value <= maxTimestamp)
        {
            // Check if already applied
            auto const isApplied = std::ranges::find(appliedIds, timestamp) != appliedIds.end();

            if (!isApplied)
            {
                manager.MarkMigrationAsApplied(*migration);
                ++markedCount;
            }
        }
    }

    return markedCount;
}

bool TransitionGlue::Initialize(Lightweight::SqlMigration::MigrationManager& manager, Lightweight::SqlConnection& connection)
{
    // Query current LUP version
    auto const currentVersion = GetCurrentLupVersion(connection);

    if (!currentVersion.has_value())
    {
        // No LASTRADA_PROPERTIES table or no version found
        // This is either a fresh database or non-LUP database
        // Nothing to transition
        return true;
    }

    // Mark all migrations up to this version as applied
    auto const markedCount = MarkMigrationsAsApplied(manager, *currentVersion);

    // Log the transition (to stderr, as this is informational)
    if (markedCount > 0)
    {
        // Note: In a real implementation, you might want to use a proper logging mechanism
        // For now, we just mark the migrations silently
    }

    return true;
}

uint64_t TransitionGlue::VersionToTimestamp(int64_t versionInteger)
{
    // LUP version integers are directly used as the lower part of the timestamp
    // e.g., 60808 -> 20000000060808
    return kTimestampPrefix + static_cast<uint64_t>(versionInteger);
}

} // namespace Lup
