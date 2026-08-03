// SPDX-License-Identifier: Apache-2.0

#include "BackupRunner.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnectInfo.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <thread>
#include <utility>

#include <QtCore/QDebug>
#include <QtCore/QMetaObject>
#include <QtCore/QRunnable>

namespace DbtoolGui
{

namespace
{

    class FunctionTask final: public QRunnable
    {
      public:
        explicit FunctionTask(std::function<void()> fn):
            _fn(std::move(fn))
        {
            setAutoDelete(true);
        }
        void run() override
        {
            try
            {
                _fn();
            }
            catch (...)
            {
            }
        }

      private:
        std::function<void()> _fn;
    };

    /// Minimal ProgressManager that forwards SqlBackup progress onto a
    /// BackupRunner's Qt signals. Kept in the .cpp so Qt headers do not leak
    /// into the runner's public header.
    class EmittingProgressManager: public Lightweight::SqlBackup::ProgressManager
    {
      public:
        explicit EmittingProgressManager(QObject* target):
            _target(target)
        {
        }

        void Update(Lightweight::SqlBackup::Progress const& p) override
        {
            auto const msg = QStringLiteral("[%1] %2 rows: %3")
                                 .arg(QString::fromStdString(std::string { p.tableName }))
                                 .arg(p.currentRows)
                                 .arg(QString::fromStdString(std::string { p.message }));
            auto const level = p.state == Lightweight::SqlBackup::Progress::State::Error     ? LogLevel::Error
                               : p.state == Lightweight::SqlBackup::Progress::State::Warning ? LogLevel::Warning
                                                                                             : LogLevel::Info;
            // Queue the signal by name directly on the target: the pointer is
            // read now (on the worker thread), and the QString/LogLevel args
            // are copied into the queued event. The manager is a stack local
            // in runBackup()/runRestore() that may be destroyed the instant
            // Backup()/Restore() returns, so capturing `this` in a deferred
            // lambda (as an earlier version did) would dereference freed
            // storage on the GUI thread. Nothing here outlives this call.
            QMetaObject::invokeMethod(
                _target, "logLine", Qt::QueuedConnection, Q_ARG(QString, msg), Q_ARG(DbtoolGui::LogLevel, level));
        }

        void AllDone() override {}

      private:
        QObject* _target;
    };

    /// Worker-thread count handed to `SqlBackup::Backup` / `Restore`. The
    /// library spawns this many internal threads to process tables in
    /// parallel; MS SQL is internally clamped to 1 by the library because of
    /// the driver's data races, so the GUI does not need a per-DBMS check.
    /// The 8-thread cap mirrors a conservative `dbtool --jobs` default and
    /// avoids saturating shared database servers.
    [[nodiscard]] unsigned BackupConcurrency() noexcept
    {
        auto const hw = std::thread::hardware_concurrency();
        return std::clamp(hw == 0 ? 1U : hw, 1U, 8U);
    }

} // namespace

BackupRunner::BackupRunner(QObject* parent):
    QObject(parent)
{
    // Serialise GUI-initiated backup / restore operations so the progress
    // pane and cancel button stay coherent. Worker-level parallelism lives
    // inside `SqlBackup::Backup` / `Restore` (see `BackupConcurrency`).
    _pool.setMaxThreadCount(1);
}

void BackupRunner::Enqueue(std::function<void()> task)
{
    _pool.start(new FunctionTask(std::move(task)));
}

void BackupRunner::setConnectionString(QString const& connectionString)
{
    _connectionString = connectionString;
}

void BackupRunner::setBusyProbe(std::function<bool()> probe)
{
    _busyProbe = std::move(probe);
}

bool BackupRunner::CanStartRun(char const* what)
{
    if (phase() != Phase::Idle)
        return false;
    if (_busyProbe && _busyProbe())
    {
        emit logLine(QStringLiteral("%1: another operation is busy (migration or managed backup in progress?).")
                         .arg(QLatin1String(what)),
                     LogLevel::Warning);
        return false;
    }
    return true;
}

void BackupRunner::runBackup(QString const& outputFile)
{
    if (!CanStartRun("Backup"))
        return;
    _phase.store(Phase::Running, std::memory_order_release);
    emit phaseChanged();

    auto* runner = this;
    auto const outFile = outputFile.toStdString();
    auto const cs = _connectionString.toStdString();

    Enqueue([runner, outFile, cs] {
        QString summary;
        bool ok = true;
        try
        {
            EmittingProgressManager pm(runner);
            Lightweight::SqlBackup::Backup(
                std::filesystem::path(outFile), Lightweight::SqlConnectionString { cs }, BackupConcurrency(), pm);
            summary = QStringLiteral("Backup wrote %1").arg(QString::fromStdString(outFile));
        }
        catch (std::exception const& e)
        {
            ok = false;
            summary = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(
            runner,
            [runner, summary, ok] {
                runner->_phase.store(Phase::Idle, std::memory_order_release);
                emit runner->phaseChanged();
                emit runner->finished(ok, summary);
            },
            Qt::QueuedConnection);
    });
}

void BackupRunner::runRestore(QString const& inputFile)
{
    if (!CanStartRun("Restore"))
        return;
    _phase.store(Phase::Running, std::memory_order_release);
    emit phaseChanged();

    auto* runner = this;
    auto const inFile = inputFile.toStdString();
    auto const cs = _connectionString.toStdString();

    Enqueue([runner, inFile, cs] {
        QString summary;
        bool ok = true;
        try
        {
            EmittingProgressManager pm(runner);
            Lightweight::SqlBackup::Restore(
                std::filesystem::path(inFile), Lightweight::SqlConnectionString { cs }, BackupConcurrency(), pm);
            summary = QStringLiteral("Restore read %1").arg(QString::fromStdString(inFile));
        }
        catch (std::exception const& e)
        {
            ok = false;
            summary = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(
            runner,
            [runner, summary, ok] {
                runner->_phase.store(Phase::Idle, std::memory_order_release);
                emit runner->phaseChanged();
                emit runner->finished(ok, summary);
            },
            Qt::QueuedConnection);
    });
}

} // namespace DbtoolGui
