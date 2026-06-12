// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../SqlTransaction.hpp"
#include "Fwd.hpp"

#include <optional>

namespace Lightweight
{
class SqlConnection;
}

namespace Lightweight::Async
{

/// A distinct coroutine-based SQL transaction.
///
/// Unlike the high- and low-level async methods (which are added directly to the existing
/// classes), a transaction is a scoped object, so it reads best as its own type. Each
/// operation is offloaded to the connection's async backend (a worker thread, serialized per
/// connection) and the awaiting coroutine resumes on the app's resume scheduler.
///
/// The underlying connection must have async enabled (SqlConnection::EnableAsync) before use.
/// Always `co_await CommitAsync()` or `co_await RollbackAsync()` explicitly; the destructor only
/// performs a best-effort *synchronous* finalization (per the configured mode) if the transaction
/// is still open, which may run on the destroying thread.
///
/// @code
/// auto tx = Async::AsyncSqlTransaction { dm.Connection() };
/// co_await tx.BeginAsync();
/// co_await dm.UpdateAsync(record);
/// co_await tx.CommitAsync();
/// @endcode
class LIGHTWEIGHT_API AsyncSqlTransaction
{
  public:
    /// Constructs an (not-yet-begun) async transaction over @p connection.
    /// @param connection The async-enabled connection to run the transaction on.
    explicit AsyncSqlTransaction(SqlConnection& connection) noexcept;

    AsyncSqlTransaction(AsyncSqlTransaction const&) = delete;
    AsyncSqlTransaction& operator=(AsyncSqlTransaction const&) = delete;
    AsyncSqlTransaction(AsyncSqlTransaction&&) = delete;
    AsyncSqlTransaction& operator=(AsyncSqlTransaction&&) = delete;

    /// Best-effort synchronous finalization if still open (see class note).
    ~AsyncSqlTransaction();

    /// Asynchronously begins the transaction (disables auto-commit).
    ///
    /// @param defaultMode How an un-finalized transaction is closed on destruction.
    /// @param isolationMode The transaction isolation level.
    /// @return A Task that completes once the transaction has begun.
    [[nodiscard]] Task<void> BeginAsync(SqlTransactionMode defaultMode = SqlTransactionMode::ROLLBACK,
                                        SqlIsolationMode isolationMode = SqlIsolationMode::DriverDefault);

    /// Asynchronously commits the transaction.
    [[nodiscard]] Task<void> CommitAsync();

    /// Asynchronously rolls back the transaction.
    [[nodiscard]] Task<void> RollbackAsync();

  private:
    SqlConnection* _connection;
    std::optional<SqlTransaction> _transaction;
};

} // namespace Lightweight::Async
