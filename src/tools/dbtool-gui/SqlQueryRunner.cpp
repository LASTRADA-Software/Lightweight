// SPDX-License-Identifier: Apache-2.0

#include "SqlQueryRunner.hpp"

#include <Lightweight/DataBinder/SqlVariant.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <QtCore/QElapsedTimer>
#include <QtCore/QMetaObject>
#include <QtCore/QRunnable>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtCore/QVariantList>

#include <sql.h>
#include <sqlext.h>

#include <exception>
#include <functional>
#include <utility>
#include <vector>

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
            catch (std::exception const&)
            {
                // The task posts its own `errorOccurred` / `finished`; this
                // catch only prevents an uncaught exception from terminating
                // the worker thread.
            }
        }

      private:
        std::function<void()> _fn;
    };

    /// Reads column display names off the live cursor via raw ODBC. The
    /// Lightweight `SqlStatement` layer has no high-level column-name API
    /// (its callers know the schema at compile time and bind to records),
    /// so we drop down to `SQLDescribeColW` / `SQLDescribeCol` here.
    QStringList ReadColumnNames(Lightweight::SqlStatement& stmt, SQLSMALLINT numColumns)
    {
        QStringList names;
        names.reserve(numColumns);
        SQLHSTMT const hStmt = stmt.NativeHandle();
        for (SQLSMALLINT i = 1; i <= numColumns; ++i)
        {
            SQLCHAR buffer[256] = {};
            SQLSMALLINT nameLen = 0;
            SQLSMALLINT dataType = 0;
            SQLULEN columnSize = 0;
            SQLSMALLINT decimalDigits = 0;
            SQLSMALLINT nullable = 0;
            auto const ret = SQLDescribeColA(hStmt,
                                             static_cast<SQLUSMALLINT>(i),
                                             buffer,
                                             static_cast<SQLSMALLINT>(sizeof(buffer)),
                                             &nameLen,
                                             &dataType,
                                             &columnSize,
                                             &decimalDigits,
                                             &nullable);
            if (SQL_SUCCEEDED(ret) && nameLen > 0)
                names << QString::fromUtf8(reinterpret_cast<char const*>(buffer), nameLen);
            else
                names << QStringLiteral("col_%1").arg(i);
        }
        return names;
    }

    void PostError(SqlQueryRunner* runner, QString const& msg, QString const& state, int native)
    {
        QMetaObject::invokeMethod(
            runner,
            [runner, msg, state, native] { emit runner->errorOccurred(msg, state, native); },
            Qt::QueuedConnection);
    }

    void PostFinished(SqlQueryRunner* runner, int rowCount, qint64 elapsedMs, QString const& status)
    {
        QMetaObject::invokeMethod(
            runner,
            [runner, rowCount, elapsedMs, status] {
                emit runner->finished(rowCount, elapsedMs, status);
            },
            Qt::QueuedConnection);
    }

    void PostResetModel(SqlResultModel* model, QStringList headers, std::vector<QVariantList> rows)
    {
        // Wrap movable state in a shared_ptr so the lambda is copy-able as
        // `std::function` requires, while still avoiding a deep-copy of the
        // rows vector on the queued-connection trampoline.
        auto payload = std::make_shared<std::pair<QStringList, std::vector<QVariantList>>>(
            std::move(headers), std::move(rows));
        QMetaObject::invokeMethod(
            model,
            [model, payload] {
                model->resetRows(std::move(payload->first), std::move(payload->second));
            },
            Qt::QueuedConnection);
    }

} // namespace

SqlQueryRunner::SqlQueryRunner(QObject* parent):
    QObject(parent),
    _model(this)
{
    // Single worker — running multiple queries concurrently is not useful
    // for an ad-hoc editor, and a serial pool keeps the model resets
    // ordered.
    _pool.setMaxThreadCount(1);
}

void SqlQueryRunner::SetBusy(bool busy)
{
    auto const previous = _busy.exchange(busy, std::memory_order_acq_rel);
    if (previous != busy)
        QMetaObject::invokeMethod(this, &SqlQueryRunner::busyChanged, Qt::QueuedConnection);
}

void SqlQueryRunner::execute(QString const& sql)
{
    if (busy())
        return;
    auto const trimmed = sql.trimmed();
    if (trimmed.isEmpty())
        return;

    SetBusy(true);
    emit started();

    auto* runner = this;
    auto* model = &_model;
    auto const queryStd = trimmed.toStdString();

    _pool.start(new FunctionTask([runner, model, queryStd] {
        QElapsedTimer timer;
        timer.start();
        try
        {
            Lightweight::SqlConnection conn;
            Lightweight::SqlStatement stmt(conn);

            auto cursor = stmt.ExecuteDirect(queryStd);
            auto const numColumns = static_cast<SQLSMALLINT>(cursor.NumColumnsAffected());

            if (numColumns == 0)
            {
                // No result set (INSERT/UPDATE/DDL). Surface row count via
                // `NumRowsAffected` so the UI can confirm the write
                // happened.
                auto const affected = static_cast<int>(cursor.NumRowsAffected());
                PostResetModel(model, QStringList {}, {});
                PostFinished(runner, affected, timer.elapsed(),
                             QStringLiteral("%1 row(s) affected").arg(affected));
                runner->SetBusy(false);
                return;
            }

            auto headers = ReadColumnNames(stmt, numColumns);
            std::vector<QVariantList> rows;
            bool truncated = false;
            while (cursor.FetchRow())
            {
                if (static_cast<int>(rows.size()) >= SqlQueryRunner::kMaxRows)
                {
                    truncated = true;
                    break;
                }
                QVariantList row;
                row.reserve(numColumns);
                for (SQLUSMALLINT i = 1; i <= static_cast<SQLUSMALLINT>(numColumns); ++i)
                {
                    // Fetch via SqlVariant so the driver picks the matching
                    // C type for each column. Forcing every column through
                    // `GetNullableColumn<std::string>` trips SQL Server's
                    // numeric-overflow check on BIGINT columns (SQLSTATE
                    // 22003) and would also lose type fidelity for dates
                    // and floats on other engines.
                    auto const variant = cursor.GetColumn<Lightweight::SqlVariant>(i);
                    if (variant.IsNull())
                        row.append(QVariant {});
                    else
                        row.append(QString::fromStdString(variant.ToString()));
                }
                rows.push_back(std::move(row));
            }

            auto const rowCount = static_cast<int>(rows.size());
            auto const elapsedMs = timer.elapsed();
            PostResetModel(model, std::move(headers), std::move(rows));
            QString const status = truncated
                ? QStringLiteral("%1 row(s) (truncated) in %2 ms").arg(rowCount).arg(elapsedMs)
                : QStringLiteral("%1 row(s) in %2 ms").arg(rowCount).arg(elapsedMs);
            PostFinished(runner, rowCount, elapsedMs, status);
        }
        catch (Lightweight::SqlException const& ex)
        {
            auto const msg = QString::fromStdString(ex.info().message);
            auto state = QString::fromStdString(ex.info().sqlState);
            if (state == QStringLiteral("     "))
                state.clear();
            PostError(runner, msg, state, static_cast<int>(ex.info().nativeErrorCode));
        }
        catch (std::exception const& ex)
        {
            PostError(runner, QString::fromUtf8(ex.what()), {}, 0);
        }
        runner->SetBusy(false);
    }));
}

} // namespace DbtoolGui
