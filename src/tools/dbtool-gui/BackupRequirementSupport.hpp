// SPDX-License-Identifier: Apache-2.0
//
// Helpers the GUI's backup requirement code shares between the runners and the controller.

#pragma once

#include <Lightweight/SqlMigration.hpp>

#include <ranges>
#include <string_view>
#include <vector>

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace DbtoolGui
{

/// The tool name the GUI's `schema_backups` records carry (see `Tools::ToolVersion`).
inline constexpr std::string_view RecordToolName = "dbtool-gui";

/// The timestamps of @p migrations, in their order.
/// @param migrations A range of `MigrationBase const*`.
/// @return One timestamp per migration.
template <std::ranges::input_range Migrations>
[[nodiscard]] std::vector<Lightweight::SqlMigration::MigrationTimestamp> TimestampsOf(Migrations const& migrations)
{
    auto timestamps = std::vector<Lightweight::SqlMigration::MigrationTimestamp> {};
    for (auto const* migration: migrations)
        timestamps.push_back(migration->GetTimestamp());
    return timestamps;
}

/// What the GUI shows of the backup requirement of the pending migrations.
struct BackupRequirementState
{
    /// Whether a required backup is missing.
    bool required = false;
    /// What requires the missing backup, e.g. `release 3.0.0`; empty while none is missing.
    QStringList reasons;
    /// Why the newest backup does not count, why the last one could not be recorded, or why the
    /// requirement could not be checked; empty when there is nothing to add.
    QString note;

    /// Compares every field.
    [[nodiscard]] bool operator==(BackupRequirementState const&) const = default;
};

/// Evaluates the backup requirement of every pending migration and pre-migrate step on the manager's
/// connection. When it cannot be evaluated, whatever declares a requirement counts as missing, with the
/// reason as the note. Runs the steps' predicates, so it belongs on the worker that owns the connection.
/// @param manager The process-wide manager, connected.
/// @param lastRecordError Why the last backup or restore could not be recorded; empty when it was.
/// @return The requirement; never throws.
[[nodiscard]] BackupRequirementState EvaluateBackupRequirement(Lightweight::SqlMigration::MigrationManager& manager,
                                                               QString const& lastRecordError);

} // namespace DbtoolGui
