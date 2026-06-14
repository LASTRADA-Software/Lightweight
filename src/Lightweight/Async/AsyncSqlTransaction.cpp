// SPDX-License-Identifier: Apache-2.0

#include "../SqlConnection.hpp"
#include "../SqlLogger.hpp"
#include "AsyncSqlTransaction.hpp"
#include "Backend.hpp"

#include <semaphore>
#include <stdexcept>
#include <utility>

namespace Lightweight::Async
{

AsyncSqlTransaction::AsyncSqlTransaction(SqlConnection& connection) noexcept:
    _connection { &connection }
{
}

AsyncSqlTransaction::~AsyncSqlTransaction()
{
    // The transaction should be finalized explicitly via co_await CommitAsync()/RollbackAsync().
    // If it is still open here, finalize it best-effort (~SqlTransaction applies the configured mode).
    if (!_transaction)
        return;

    SqlLogger::GetLogger().OnWarning("AsyncSqlTransaction destroyed while still open; finalizing it now. "
                                     "Prefer explicit co_await CommitAsync()/RollbackAsync().");

    if (_connection->IsAsyncEnabled())
    {
        // Route the finalization through the connection's strand so ~SqlTransaction touches the ODBC
        // handle only on the strand — never concurrently with another in-flight async op on this
        // connection. Block the destroying thread until the strand has run it. (Must not be called
        // from within a strand op on this same connection: that would wait on its own strand.)
        std::binary_semaphore done { 0 };
        _connection->AsyncBackend().Strand().Post([this, &done] {
            _transaction.reset(); // ~SqlTransaction is noexcept; finalizes per the configured mode
            done.release();
        });
        done.acquire();
    }
    else
    {
        // Async already disabled (e.g. the owning pooled mapper was returned): nothing can be in
        // flight on this connection, so finalizing synchronously on this thread is safe.
        _transaction.reset();
    }
}

Task<void> AsyncSqlTransaction::BeginAsync(SqlTransactionMode defaultMode,
                                           SqlIsolationMode isolationMode,
                                           CancellationToken token)
{
    return RunAsync(
        _connection->AsyncBackend(),
        [this, defaultMode, isolationMode] {
            // Re-begin without an intervening commit/rollback would silently discard the first
            // transaction's work; treat it as a programmer error (checked on the strand, serialized
            // with any prior BeginAsync) surfaced as an exception at the co_await site.
            if (_transaction)
                throw std::logic_error { "AsyncSqlTransaction::BeginAsync: a transaction is already open." };
            _transaction.emplace(*_connection, defaultMode, isolationMode);
        },
        std::move(token));
}

Task<void> AsyncSqlTransaction::CommitAsync(CancellationToken token)
{
    return RunAsync(
        _connection->AsyncBackend(),
        [this] {
            if (_transaction)
            {
                _transaction->Commit();
                _transaction.reset();
            }
        },
        std::move(token));
}

Task<void> AsyncSqlTransaction::RollbackAsync(CancellationToken token)
{
    return RunAsync(
        _connection->AsyncBackend(),
        [this] {
            if (_transaction)
            {
                _transaction->Rollback();
                _transaction.reset();
            }
        },
        std::move(token));
}

} // namespace Lightweight::Async
