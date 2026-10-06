// SPDX-License-Identifier: Apache-2.0
//
// Maps SqlBackup's per-table progress states onto the state strings of
// `BackupTableListModel`. Shared by the managed-backup controller and the
// ad-hoc `BackupRunner`, so a table reads the same in the detail panel whichever
// of them drove the run.

#pragma once

#include <Lightweight/SqlBackup.hpp>

#include <QtCore/QString>

namespace DbtoolGui
{

/// Maps a Progress state onto the per-table model's state string.
/// @param state The state reported by SqlBackup.
/// @return "running", "done", "error" or "warning".
[[nodiscard]] inline QString TableStateString(Lightweight::SqlBackup::Progress::State state)
{
    switch (state)
    {
        case Lightweight::SqlBackup::Progress::State::Started:
        case Lightweight::SqlBackup::Progress::State::InProgress:
            return QStringLiteral("running");
        case Lightweight::SqlBackup::Progress::State::Finished:
            return QStringLiteral("done");
        case Lightweight::SqlBackup::Progress::State::Error:
            return QStringLiteral("error");
        case Lightweight::SqlBackup::Progress::State::Warning:
            return QStringLiteral("warning");
    }
    return QStringLiteral("running");
}

} // namespace DbtoolGui
