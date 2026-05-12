// SPDX-License-Identifier: Apache-2.0
//
// `SqlQueryRunner` is the GUI's asynchronous wrapper around an ad-hoc SQL
// execution. It mirrors the structure of `MigrationRunner`: a single-thread
// `QThreadPool`, signals marshalled back onto the GUI thread via
// `QMetaObject::invokeMethod(... Qt::QueuedConnection)`, and a status
// machine ("busy" flag) that QML binds against.
//
// The result-set is materialised into an owned `SqlResultModel` snapshot
// rather than streamed row-by-row — query editor users issue queries small
// enough to fit comfortably in memory, and the runner enforces a hard row
// cap to keep `SELECT *` against a 10M-row table from OOM-ing the GUI.

#pragma once

#include "Models/SqlResultModel.hpp"

#include <atomic>
#include <stop_token>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QThreadPool>
#include <QtQmlIntegration/QtQmlIntegration>

namespace DbtoolGui
{

class SqlQueryRunner: public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(SqlResultModel* model READ model CONSTANT)
  public:
    /// Maximum number of rows materialised from a single result set. A
    /// runaway `SELECT *` past this cap is silently truncated and the
    /// `finished` signal's status string surfaces "(truncated)" to the UI.
    static constexpr int kMaxRows = 10'000;

    explicit SqlQueryRunner(QObject* parent = nullptr);

    [[nodiscard]] bool busy() const noexcept
    {
        return _busy.load(std::memory_order_acquire);
    }
    [[nodiscard]] SqlResultModel* model() noexcept
    {
        return &_model;
    }

    /// Enqueues an `ExecuteDirect` of `sql` on the worker thread. No-op if a
    /// query is already in flight (Execute is gated by `busy` in the UI as
    /// well).
    Q_INVOKABLE void execute(QString const& sql);

    /// Requests cancellation of an in-flight query, if any. Sets the stop
    /// source seen by the worker lambda *and* posts an asynchronous
    /// `SQLCancel` against the active statement handle so the worker is
    /// interrupted while it is blocked inside `SQLExecDirect` / `SQLFetch`.
    /// No-op when `busy()` is `false`.
    Q_INVOKABLE void cancel();

  signals:
    void busyChanged();
    void started();
    /// Successful run. `rowCount` is the number of rows in the result set
    /// (0 for non-result-set statements like `INSERT`); `elapsedMs` is wall
    /// time including connection acquisition; `status` is a human-readable
    /// summary suitable for display in the editor's status label.
    void finished(int rowCount, qint64 elapsedMs, QString status);
    /// SQL or driver error. `sqlState` may be empty for non-SQL errors;
    /// `nativeError` is 0 in that case as well.
    void errorOccurred(QString message, QString sqlState, int nativeError);

  private:
    void SetBusy(bool busy);

    QThreadPool _pool;
    std::atomic<bool> _busy { false };
    /// Replaced with a fresh source on every `execute()` so a stop request
    /// from a previous run does not leak into the next query.
    std::stop_source _stopSource;
    /// Native ODBC statement handle of the currently-executing query, or
    /// `nullptr` when no query is in flight. Atomic so `cancel()` can read
    /// it from the GUI thread while the worker thread writes/clears it.
    /// Stored as `void*` to keep `<sql.h>` out of the public header — the
    /// `.cpp` reinterpret-casts to `SQLHSTMT` before invoking `SQLCancel`.
    std::atomic<void*> _activeStmt { nullptr };
    SqlResultModel _model;
};

} // namespace DbtoolGui
