// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Api.hpp"
#include "../SqlTransaction.hpp"
#include "Fwd.hpp"

#include <optional>
#include <stop_token>

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
/// The underlying connection must have async enabled (SqlConnection::EnableAsync) before use and
/// must stay async-enabled and alive for the whole lifetime of the transaction: do not destroy the
/// connection (or return the owning pooled DataMapper, which disables async) between @ref BeginAsync
/// and the matching @ref CommitAsync / @ref RollbackAsync — the offloaded steps capture the
/// connection by pointer and would otherwise fail (a clear @c std::logic_error from
/// @c SqlConnection::AsyncBackend) or dangle.
///
/// Always `co_await CommitAsync()` or `co_await RollbackAsync()` explicitly. If the transaction is
/// still open when destroyed, the destructor performs a best-effort finalization (per the configured
/// mode) and emits a warning via @c SqlLogger. That finalization is itself routed through the
/// connection's strand (and blocks the destroying thread until it completes) so it never touches the
/// ODBC handle concurrently with another in-flight async operation on the same connection.
///
/// @warning The destructor's strand-serialized finalization blocks the destroying thread until the
/// strand has run it, so do not destroy an open transaction from any thread that the strand needs in
/// order to make progress. That includes (a) destroying it from @e within a strand operation on its own
/// connection, and (b) — in the multi-threaded model where the resume scheduler @e is the worker pool
/// that backs the connection's strand — letting it be destroyed while the coroutine is resuming on one
/// of those worker threads (with a single-worker pool this self-waits and deadlocks). The connection
/// and its injected executors must also outlive the transaction; tearing the worker pool down first
/// leaves the finalization undrained and the destructor blocked. Prefer explicit
/// @ref CommitAsync / @ref RollbackAsync so the destructor never has to finalize.
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
    /// @param defaultMode How an un-finalized transaction is closed on destruction. Defaults to
    ///        @c COMMIT to match the synchronous @ref SqlTransaction, so porting sync code that
    ///        relies on commit-on-scope-exit does not silently switch to rollback.
    /// @param isolationMode The transaction isolation level.
    /// @param token Optional cancellation token (a default-constructed @c std::stop_token is
    ///        non-cancellable; cancellation is checked before the step is dispatched).
    /// @return A Task that completes once the transaction has begun.
    /// @throws std::logic_error if a transaction is already open on this object (programmer error).
    [[nodiscard]] Task<void> BeginAsync(SqlTransactionMode defaultMode = SqlTransactionMode::COMMIT,
                                        SqlIsolationMode isolationMode = SqlIsolationMode::DriverDefault,
                                        std::stop_token token = {});

    /// Asynchronously commits the transaction.
    /// @note Finalization is a point of no return and is intentionally @b not cancellable: it always
    ///       runs to completion. (Abandoning it would leave the transaction open, and the destructor
    ///       would then finalize it with the configured default mode.)
    [[nodiscard]] Task<void> CommitAsync();

    /// Asynchronously rolls back the transaction.
    /// @note Finalization is a point of no return and is intentionally @b not cancellable: it always
    ///       runs to completion (so a rollback never silently degrades into a commit-on-destruction).
    [[nodiscard]] Task<void> RollbackAsync();

  private:
    /// Runs @p finalize (SqlTransaction::Commit or ::Rollback) on the open transaction via the
    /// connection strand, then clears it. Shared by CommitAsync/RollbackAsync. Not cancellable.
    /// @param finalize The finalizing member function to invoke.
    [[nodiscard]] Task<void> FinalizeAsync(void (SqlTransaction::*finalize)());

    SqlConnection* _connection;
    std::optional<SqlTransaction> _transaction;
};

} // namespace Lightweight::Async
