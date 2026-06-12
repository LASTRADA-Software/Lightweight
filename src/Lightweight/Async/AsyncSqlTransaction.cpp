// SPDX-License-Identifier: Apache-2.0

#include "../SqlConnection.hpp"
#include "../SqlLogger.hpp"
#include "AsyncSqlTransaction.hpp"
#include "Backend.hpp"

namespace Lightweight::Async
{

AsyncSqlTransaction::AsyncSqlTransaction(SqlConnection& connection) noexcept:
    _connection { &connection }
{
}

AsyncSqlTransaction::~AsyncSqlTransaction()
{
    // Surface the misuse: the transaction should be finalized explicitly via co_await
    // CommitAsync()/RollbackAsync(). If it is still open here, ~SqlTransaction (below) performs a
    // best-effort *synchronous* finalization on the destroying thread, off the connection's strand
    // — which is only safe if no async operation is concurrently in flight on this connection.
    if (_transaction)
        SqlLogger::GetLogger().OnWarning(
            "AsyncSqlTransaction destroyed while still open; finalizing synchronously off-strand. "
            "Prefer explicit co_await CommitAsync()/RollbackAsync() and ensure the connection is idle.");
}

Task<void> AsyncSqlTransaction::BeginAsync(SqlTransactionMode defaultMode, SqlIsolationMode isolationMode)
{
    return RunAsync(_connection->AsyncBackend(),
                    [this, defaultMode, isolationMode] { _transaction.emplace(*_connection, defaultMode, isolationMode); });
}

Task<void> AsyncSqlTransaction::CommitAsync()
{
    return RunAsync(_connection->AsyncBackend(), [this] {
        if (_transaction)
        {
            _transaction->Commit();
            _transaction.reset();
        }
    });
}

Task<void> AsyncSqlTransaction::RollbackAsync()
{
    return RunAsync(_connection->AsyncBackend(), [this] {
        if (_transaction)
        {
            _transaction->Rollback();
            _transaction.reset();
        }
    });
}

} // namespace Lightweight::Async
