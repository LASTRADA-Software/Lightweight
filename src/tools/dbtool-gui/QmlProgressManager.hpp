// SPDX-License-Identifier: Apache-2.0
//
// `QmlProgressManager` mirrors the callback surface that
// `Lightweight::SqlBackup::ProgressManager` exposes to dbtool's
// `StandardProgressManager`, but instead of printing to stdout it emits Qt
// signals that the view-model marshals to the GUI thread. Keeping the exact
// same callback shape means we do not fork progress semantics between the
// CLI and the GUI — the mockup's "dry-run output should match
// `dbtool migrate --dry-run` byte-for-byte" acceptance criterion depends on
// this.
//
// The class lives in the GUI target only. CLI builds continue to use
// `Tools::StandardProgressManager` from `src/tools/dbtool/`.

#pragma once

#include <Lightweight/SqlBackup.hpp>

#include <QtCore/QObject>

namespace DbtoolGui
{

class QmlProgressManager final: public QObject, public Lightweight::SqlBackup::ProgressManager
{
    Q_OBJECT
  public:
    explicit QmlProgressManager(QObject* parent = nullptr);

    /// `ProgressManager::Update` override. Called from a worker thread during
    /// migration / backup runs — we use queued signals so the UI sees
    /// consistent state.
    void Update(Lightweight::SqlBackup::Progress const& progress) override;

    /// `ProgressManager::AllDone` override. Ends a run; consumers typically
    /// pair this with `MigrationRunner::finished`.
    void AllDone() override;

  signals:
    /// Mirrors `Progress` as a structured Qt signal. Consumers in QML match
    /// on `state` and render the row / progress bar accordingly.
    void progressUpdated(QString tableName, int state, qulonglong currentRows, qulonglong totalRows, QString message);

    /// Emitted exactly once per run, after the last `progressUpdated`.
    void allDone();
};

} // namespace DbtoolGui
