// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"
#include "../SqlMigration.hpp"

#include <optional>
#include <string_view>

namespace Lightweight::MigrationFold
{

/// @brief Re-export of `SqlMigration::MigrationManager::PlanFoldingResult` so callers
/// in this namespace can use a shorter spelling.
using FoldResult = SqlMigration::MigrationManager::PlanFoldingResult;

/// @brief Resolves an `--up-to <X>` argument to a `MigrationTimestamp`. Accepts:
///
///   - The empty string → latest registered release. Throws if no releases exist.
///   - All-digit strings → parsed as a raw timestamp.
///   - Anything else → looked up as a release version. Throws if unknown.
///
/// Centralised here so the dbtool command and any future caller resolve `--up-to`
/// identically.
[[nodiscard]] LIGHTWEIGHT_API SqlMigration::MigrationTimestamp ResolveUpTo(
    SqlMigration::MigrationManager const& manager, std::string_view raw);

/// @brief Folds the given manager's migrations up to (optionally) `upToInclusive`,
/// using the supplied formatter to build the per-migration plans. Wrapper around
/// `MigrationManager::FoldRegisteredMigrations`.
[[nodiscard]] LIGHTWEIGHT_API FoldResult Fold(SqlMigration::MigrationManager const& manager,
                                              SqlQueryFormatter const& formatter,
                                              std::optional<SqlMigration::MigrationTimestamp> upToInclusive = std::nullopt);

} // namespace Lightweight::MigrationFold
