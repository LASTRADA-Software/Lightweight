// SPDX-License-Identifier: Apache-2.0
//
// `MigrationSnapshot` is what the GUI shows of the migration manager: the rows of the migration and
// release models plus the figures derived from the plugin set. It is captured on the worker thread
// that owns the migration connection, so the GUI thread never reads the manager itself.

#pragma once

#include "MigrationListModel.hpp"
#include "ReleaseListModel.hpp"

#include <cstdint>
#include <vector>

#include <QtCore/QString>

// Forward declaration: see MigrationRunner.hpp. Including
// `<Lightweight/SqlMigration.hpp>` here causes Qt's `qmltyperegistrar` to
// re-anchor the surrounding `DbtoolGui` namespace inside `Lightweight::` in
// the generated `*_qmltyperegistrations.cpp`, which then fails to compile.
namespace Lightweight
{
namespace SqlMigration
{
    class MigrationManager;
}
} // namespace Lightweight

namespace DbtoolGui
{

/// How much of the database a snapshot consults for the applied state of each migration.
enum class AppliedState : std::uint8_t
{
    Unknown,     ///< Nothing: no database is open, so every migration is "unknown".
    PluginsOnly, ///< Nothing: the database has no readable history, so every migration is "pending".
    Database,    ///< The history and the checksums; falls back to `PluginsOnly` with a warning when they cannot be read.
};

/// The models' rows and derived figures for one state of the plugins and the database.
struct MigrationSnapshot
{
    /// Rows of the migration model, newest first.
    std::vector<MigrationRow> migrations;
    /// Rows of the release model, newest first.
    std::vector<ReleaseRow> releases;
    /// The pre-migrate steps registered on the manager.
    int preMigrateStepCount = 0;
    /// The release the database would be at after applying every pending migration (see `AppController`).
    QString targetReleaseLabel;
    /// Why the history could not be read, e.g. "Could not read migration history: …"; empty otherwise.
    QString warning;
};

/// Captures the rows and figures of @p manager. Only `AppliedState::Database` reads the database.
/// @param manager The manager holding the loaded plugins' migrations, releases and steps.
/// @param state How much of the database to consult.
/// @return The snapshot; never throws for an unreadable history (see `MigrationSnapshot::warning`).
[[nodiscard]] MigrationSnapshot CaptureMigrationSnapshot(Lightweight::SqlMigration::MigrationManager const& manager,
                                                         AppliedState state);

} // namespace DbtoolGui
