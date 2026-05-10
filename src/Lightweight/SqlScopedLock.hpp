// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlAdvisoryLock.hpp"
#include "SqlConnection.hpp"

#include <chrono>
#include <expected>
#include <string>
#include <string_view>

namespace Lightweight
{

/// RAII-style cross-process advisory lock.
///
/// Provides a distributed locking mechanism so that only one process at a
/// time can hold a named token. The dialect-specific primitive is selected
/// by the active `SqlQueryFormatter`'s `AdvisoryLockOps()` handler:
/// - SQL Server: `sp_getapplock` / `sp_releaseapplock`
/// - PostgreSQL: `pg_advisory_lock` / `pg_advisory_unlock`
/// - SQLite:     `_lightweight_locks` table guarded by a unique constraint
///
/// `SqlScopedLock` itself contains zero per-DBMS branching — adding a new
/// dialect only requires implementing a `SqlAdvisoryLockHandler` and wiring
/// it to the new formatter's `AdvisoryLockOps()`.
///
/// @code
/// // Throws on failure (timeout, deadlock, or driver error):
/// auto lock = SqlScopedLock { connection, "my-resource" };
///
/// // Or, with structured error handling:
/// auto maybeLock = SqlScopedLock::TryConstruct(connection, "my-resource");
/// if (!maybeLock)
///     std::println(stderr, "{}", maybeLock.error().message);
/// @endcode
class SqlScopedLock
{
  public:
    /// Acquire a named advisory lock.
    ///
    /// @param connection Database connection to use for locking.
    /// @param lockName Name of the lock (cooperative; any string).
    /// @param timeout Maximum time to wait for lock acquisition.
    /// @throws std::runtime_error if the lock cannot be acquired (timeout, deadlock,
    ///         or driver error). The thrown exception's `what()` contains a
    ///         human-readable explanation. For programmatic access to the failure
    ///         reason, use the non-throwing `TryConstruct` factory.
    LIGHTWEIGHT_API explicit SqlScopedLock(SqlConnection& connection,
                                           std::string_view lockName,
                                           std::chrono::milliseconds timeout = std::chrono::seconds(30));

    /// Non-throwing factory that returns either a held lock or a structured
    /// `SqlLockError` describing why the acquire failed. Prefer this when
    /// the caller wants to react programmatically to the failure reason
    /// (timeout vs deadlock vs driver error) — for example, to retry with
    /// backoff or to show a tailored UI message.
    [[nodiscard]] LIGHTWEIGHT_API static std::expected<SqlScopedLock, SqlLockError> TryConstruct(
        SqlConnection& connection, std::string_view lockName, std::chrono::milliseconds timeout = std::chrono::seconds(30));

    /// Releases the lock on destruction. Release-time errors are routed to
    /// `SqlLogger::OnWarning` rather than being silently swallowed.
    LIGHTWEIGHT_API ~SqlScopedLock();

    SqlScopedLock(SqlScopedLock const&) = delete;
    SqlScopedLock& operator=(SqlScopedLock const&) = delete;

    /// Move constructor.
    LIGHTWEIGHT_API SqlScopedLock(SqlScopedLock&& other) noexcept;

    /// Move assignment operator.
    LIGHTWEIGHT_API SqlScopedLock& operator=(SqlScopedLock&& other) noexcept;

    /// Check if the lock is currently held by this instance.
    [[nodiscard]] bool IsLocked() const noexcept
    {
        return _locked;
    }

    /// Returns the lock name passed at construction.
    [[nodiscard]] std::string_view Name() const noexcept
    {
        return _lockName;
    }

    /// Release the lock early.
    ///
    /// Automatically called in the destructor; can be invoked manually for
    /// finer scope control. Returns the underlying `SqlLockError` if the
    /// release round-trip fails so callers can log or surface it.
    [[nodiscard]] LIGHTWEIGHT_API std::expected<void, SqlLockError> Release();

  private:
    /// Internal constructor used by `TryConstruct` to skip the throwing
    /// acquire path — the caller has already inspected the handler's
    /// `TryAcquire` result and confirmed success.
    struct AlreadyLockedTag
    {
    };
    SqlScopedLock(SqlConnection& connection,
                  std::string_view lockName,
                  SqlAdvisoryLockHandler const& handler,
                  AlreadyLockedTag /*tag*/) noexcept;

    SqlConnection* _connection;
    std::string _lockName;
    SqlAdvisoryLockHandler const* _handler { nullptr };
    bool _locked { false };
};

} // namespace Lightweight
