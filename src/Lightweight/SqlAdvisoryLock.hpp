// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlError.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight
{

class SqlConnection;

/// Reason an advisory-lock operation failed.
///
/// Carried inside `SqlLockError` so callers can decide whether to retry,
/// escalate, or surface the cause to a human — without resorting to
/// `what()`-string text matching.
enum class SqlLockFailureReason : uint8_t
{
    /// Lock wasn't acquired within the requested timeout.
    Timeout,
    /// Server picked us as the deadlock victim (e.g. SQL Server `sp_getapplock` = -3).
    Deadlock,
    /// Lock request cancelled by the server (e.g. SQL Server `sp_getapplock` = -2).
    Cancelled,
    /// Backend reported a parameter or validation error (e.g. SQL Server `sp_getapplock` = -999).
    ParameterError,
    /// Any other driver/server-level failure; `SqlLockError::info` carries the SQLSTATE detail.
    DriverError,
};

/// Structured error returned by `SqlAdvisoryLockHandler::TryAcquire` and
/// `SqlAdvisoryLockHandler::Release`.
///
/// Lets callers propagate the *root cause* of a lock failure (timeout vs.
/// deadlock vs. driver-level fault) instead of having to text-match an
/// exception message.
struct SqlLockError
{
    /// What went wrong, at the granularity callers actually care about.
    SqlLockFailureReason reason {};
    /// The lock name that was being acquired or released — handy when several
    /// distinct locks coexist in a process.
    std::string lockName;
    /// Requested timeout (zero for `Release`).
    std::chrono::milliseconds timeout {};
    /// Pre-formatted, human-readable explanation. Already includes the lock
    /// name and timeout where relevant, so callers can `std::println("{}", err.message)`
    /// without further formatting.
    std::string message;
    /// Set on `DriverError`; carries SQLSTATE / native error / driver text.
    std::optional<SqlErrorInfo> info;
};

/// Dialect-specific implementation of advisory-lock acquire/release.
///
/// Each `SqlQueryFormatter` returns a process-singleton instance of the
/// appropriate concrete handler via `SqlQueryFormatter::AdvisoryLockOps()`.
/// Concrete implementations live next to the formatter that owns them
/// (e.g. the SQL Server handler is defined alongside `SqlServerQueryFormatter`),
/// keeping every dialect quirk in one source file and out of the
/// `SqlScopedLock` business logic.
///
/// Callers that need a lock should use the `SqlScopedLock` RAII wrapper —
/// this type is the extension point for adding new dialects.
///
/// Naming: these are *advisory* locks (also called application or named
/// locks): they don't lock any particular row or table; they're purely
/// cooperative tokens identified by a string name. Two processes that
/// agree to acquire the same name serialise on it.
class [[nodiscard]] LIGHTWEIGHT_API SqlAdvisoryLockHandler
{
  public:
    SqlAdvisoryLockHandler() = default;
    /// Polymorphic destructor.
    virtual ~SqlAdvisoryLockHandler() = default;

    SqlAdvisoryLockHandler(SqlAdvisoryLockHandler const&) = delete;
    SqlAdvisoryLockHandler& operator=(SqlAdvisoryLockHandler const&) = delete;
    SqlAdvisoryLockHandler(SqlAdvisoryLockHandler&&) = delete;
    SqlAdvisoryLockHandler& operator=(SqlAdvisoryLockHandler&&) = delete;

    /// Attempts to acquire the named lock on `connection`, blocking up to `timeout` ms.
    ///
    /// Returns an empty `expected` on success; otherwise a fully-populated
    /// `SqlLockError` so the caller can distinguish timeout from deadlock
    /// from driver error and propagate accordingly.
    ///
    /// Implementations MUST NOT throw on a recoverable timeout — that's a
    /// `SqlLockFailureReason::Timeout`. They MAY throw only on truly
    /// catastrophic conditions (out-of-memory, ABI mismatch); routine driver
    /// failures should populate `SqlLockFailureReason::DriverError` with
    /// `info` set.
    ///
    /// @param connection The SQL connection to acquire the lock on.
    /// @param lockName The name of the lock (advisory; backends hash or quote as needed).
    /// @param timeout Maximum time to wait for lock acquisition.
    /// @return Empty `expected` on success, populated `SqlLockError` on failure.
    [[nodiscard]] virtual std::expected<void, SqlLockError> TryAcquire(SqlConnection& connection,
                                                                       std::string_view lockName,
                                                                       std::chrono::milliseconds timeout) const = 0;

    /// Releases the named lock previously acquired with `TryAcquire`.
    ///
    /// Implementations MUST tolerate "release of a lock that's already gone"
    /// (connection teardown, server-side session expiry, etc.) as success —
    /// `Release` is idempotent by contract.
    ///
    /// Returns an error only if the release round-trip itself failed. The
    /// caller (typically `SqlScopedLock`'s destructor) should log such an
    /// error rather than swallow it silently.
    ///
    /// @param connection The SQL connection that holds the lock.
    /// @param lockName The name of the lock to release.
    /// @return Empty `expected` on success, populated `SqlLockError` on failure.
    [[nodiscard]] virtual std::expected<void, SqlLockError> Release(SqlConnection& connection,
                                                                    std::string_view lockName) const = 0;

    /// Names of any backend-internal bookkeeping tables this handler
    /// maintains — empty for backends with server-native advisory locks
    /// (SQL Server's `sp_getapplock`, PostgreSQL's `pg_advisory_lock`),
    /// non-empty for backends that implement locking via a regular table
    /// (SQLite returns its lock-table name here).
    ///
    /// Tooling that walks the live schema (`dbtool hard-reset`, schema
    /// diffs, backups that try to skip internal tables) consults this so
    /// the lock table is recognised as infrastructure rather than mistaken
    /// for user data.
    [[nodiscard]] virtual std::vector<std::string_view> BookkeepingTableNames() const noexcept
    {
        return {};
    }
};

/// @brief Returns the SQLite-specific singleton handler.
///
/// Defined in `SqlQueryFormatter.cpp` so the lock execution code (which needs
/// `SqlConnection` / `SqlStatement` includes) doesn't bleed into the
/// formatter headers. The formatter overrides delegate to these free
/// functions inline, which keeps every formatter's vtable weak — the same
/// emission shape as before this refactor, so the shared-library build
/// doesn't need a class-level `LIGHTWEIGHT_API` retrofitted onto the
/// concrete formatter types.
[[nodiscard]] LIGHTWEIGHT_API SqlAdvisoryLockHandler const& SqliteAdvisoryLockOps();

/// @brief Returns the SQL Server-specific singleton handler. See `SqliteAdvisoryLockOps()`.
[[nodiscard]] LIGHTWEIGHT_API SqlAdvisoryLockHandler const& SqlServerAdvisoryLockOps();

/// @brief Returns the PostgreSQL-specific singleton handler. See `SqliteAdvisoryLockOps()`.
[[nodiscard]] LIGHTWEIGHT_API SqlAdvisoryLockHandler const& PostgreSqlAdvisoryLockOps();

} // namespace Lightweight
