// SPDX-License-Identifier: Apache-2.0

#include "BackupRunner.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlConnectInfo.hpp>

#include <exception>
#include <filesystem>
#include <utility>

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
    /// BackupRunner's Qt signals. Kept in the .cpp so QtKeychain / Qt headers
    /// do not leak into the runner's public header.
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
            auto const level = static_cast<int>(p.state == Lightweight::SqlBackup::Progress::State::Error     ? 2
                                                : p.state == Lightweight::SqlBackup::Progress::State::Warning ? 1
                                                                                                              : 0);
            QMetaObject::invokeMethod(
                _target,
                [this, msg, level] {
                    QMetaObject::invokeMethod(
                        _target, "logLine", Qt::DirectConnection, Q_ARG(QString, msg), Q_ARG(int, level));
                },
                Qt::QueuedConnection);
        }

        void AllDone() override {}

      private:
        QObject* _target;
    };

} // namespace

BackupRunner::BackupRunner(QObject* parent):
    QObject(parent)
{
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

void BackupRunner::runBackup(QString const& outputFile)
{
    if (phase() != Phase::Idle)
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
            Lightweight::SqlBackup::Backup(std::filesystem::path(outFile),
                                           Lightweight::SqlConnectionString { cs },
                                           /*concurrency=*/1,
                                           pm);
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
    if (phase() != Phase::Idle)
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
            Lightweight::SqlBackup::Restore(std::filesystem::path(inFile),
                                            Lightweight::SqlConnectionString { cs },
                                            /*concurrency=*/1,
                                            pm);
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
