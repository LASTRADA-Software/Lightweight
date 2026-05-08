// SPDX-License-Identifier: Apache-2.0
//
// `BackupRunner` mirrors `MigrationRunner` for the SqlBackup API. The GUI
// uses it to drive the fourth toolbar action (Backup / Restore) — the same
// async worker pattern, so progress signals surface in the same log panel
// users already know from migration runs.

#pragma once

#include <atomic>
#include <functional>

#include <QtCore/QObject>
#include <QtCore/QThreadPool>
#include <QtQmlIntegration/QtQmlIntegration>

namespace Lightweight
{
struct SqlConnectionString;
namespace SqlBackup
{
    struct ProgressManager;
}
} // namespace Lightweight

namespace DbtoolGui
{

class BackupRunner: public QObject
{
    Q_OBJECT
    QML_ELEMENT
  public:
    enum class Phase : int
    {
        Idle,
        Running,
    };
    Q_ENUM(Phase)

    // See MigrationRunner.hpp for the rationale — without Q_PROPERTY the
    // `phase` binding in QML is a stuck method handle, not a live value.
    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)

    explicit BackupRunner(QObject* parent = nullptr);

    /// Connection string used for the SqlBackup / Restore API calls. Left
    /// empty means "use whatever was set via `SqlConnection::SetDefault*`".
    Q_INVOKABLE void setConnectionString(QString const& connectionString);

    Q_INVOKABLE void runBackup(QString const& outputFile);
    Q_INVOKABLE void runRestore(QString const& inputFile);

    [[nodiscard]] Phase phase() const noexcept
    {
        return _phase.load(std::memory_order_acquire);
    }

  signals:
    void logLine(QString line, int level);
    void finished(bool ok, QString summary);
    void phaseChanged();

  private:
    void Enqueue(std::function<void()> task);

    QThreadPool _pool;
    std::atomic<Phase> _phase { Phase::Idle };
    QString _connectionString;
};

} // namespace DbtoolGui
