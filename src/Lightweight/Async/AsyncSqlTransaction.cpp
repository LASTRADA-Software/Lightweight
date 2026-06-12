// SPDX-License-Identifier: Apache-2.0

#include "../SqlConnection.hpp"
#include "AsyncSqlTransaction.hpp"
#include "Backend.hpp"

namespace Lightweight::Async
{

AsyncSqlTransaction::AsyncSqlTransaction(SqlConnection& connection) noexcept:
    _connection { &connection }
{
}

AsyncSqlTransaction::~AsyncSqlTransaction() = default;

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
