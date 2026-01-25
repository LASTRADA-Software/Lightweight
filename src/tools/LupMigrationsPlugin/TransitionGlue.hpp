// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <cstdint>
#include <optional>

namespace Lup
{

/// @brief Handles transition from LASTRADA_PROPERTIES to schema_migrations.
///
/// This class provides utilities to migrate from the legacy LUP version tracking
/// (stored in LASTRADA_PROPERTIES table) to the modern schema_migrations table
/// used by Lightweight's migration system.
class TransitionGlue
{
  public:
    /// @brief Query current LUP version from LASTRADA_PROPERTIES table.
    ///
    /// The version is stored in the VALUE column where NR=4.
    ///
    /// @param connection Active database connection.
    /// @return The current LUP version as integer (e.g., 60808 for version 6.8.8),
    ///         or nullopt if the table doesn't exist or the version is not found.
    [[nodiscard]] static std::optional<int64_t> GetCurrentLupVersion(Lightweight::SqlConnection& connection);

    /// @brief Mark all migrations up to given version as applied in schema_migrations.
    ///
    /// This allows seamless transition without re-running old migrations.
    ///
    /// @param manager The migration manager containing all registered migrations.
    /// @param maxVersionInteger Maximum version to mark as applied (inclusive).
    /// @return Number of migrations marked as applied.
    static size_t MarkMigrationsAsApplied(Lightweight::SqlMigration::MigrationManager& manager, int64_t maxVersionInteger);

    /// @brief Initialize transition - call once on first dbtool run.
    ///
    /// This method:
    /// 1. Queries LASTRADA_PROPERTIES for current version
    /// 2. Marks all migrations up to that version as applied (in schema_migrations)
    ///
    /// If LASTRADA_PROPERTIES doesn't exist or has no version, this is treated as
    /// a fresh database and no migrations are marked as applied.
    ///
    /// @param manager The migration manager.
    /// @param connection Active database connection.
    /// @return true if transition was performed (or not needed), false on error.
    static bool Initialize(Lightweight::SqlMigration::MigrationManager& manager, Lightweight::SqlConnection& connection);

    /// @brief Convert LUP version integer to migration timestamp.
    ///
    /// LUP versions like 60808 are converted to timestamps like 20000000060808.
    ///
    /// @param versionInteger The LUP version integer.
    /// @return The corresponding migration timestamp.
    [[nodiscard]] static uint64_t VersionToTimestamp(int64_t versionInteger);
};

} // namespace Lup
