// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlError.hpp"

#include <cstdint>

namespace Lightweight
{

/// @defgroup Retry Retry and Backoff
/// @brief A reusable, injectable retry/backoff policy for transient database failures.
///
/// The pieces are deliberately split so each one is usable — and testable — on its own:
///
/// - @ref SqlRetryClassifier decides whether a given @ref SqlErrorInfo is worth another attempt.
///   It is a per-DBMS extension point, reached through @c SqlQueryFormatter::RetryOps().
/// - @ref SqlRetrySettings carries the backoff knobs (budget, initial delay, multiplier, caps).
/// - @ref SqlRetryPolicy combines the two into a decision function and a small driver that runs a
///   callable until it succeeds or the policy gives up.

/// @ingroup Retry
/// Whether a failed operation is worth attempting again.
enum class SqlErrorTransience : std::uint8_t
{
    /// The condition is permanent — a constraint violation, a syntax error, a missing table.
    /// Retrying it will fail identically, so the caller should surface the failure.
    Permanent,

    /// The condition is temporary — a dropped connection, a lock timeout, a deadlock-victim
    /// rollback. The very same statement may well succeed on a later attempt.
    Transient,
};

/// @ingroup Retry
/// @brief Dialect-specific classification of SQL errors into transient and permanent.
///
/// Every @c SqlQueryFormatter returns a process-singleton instance of the appropriate concrete
/// classifier via @c SqlQueryFormatter::RetryOps(), which is what keeps per-DBMS error-code
/// knowledge out of business logic: callers ask the formatter, never @c SqlServerType directly.
///
/// The dialects genuinely disagree. PostgreSQL reports a serialization failure as SQLSTATE
/// @c 40001 and a lock it could not take as @c 55P03; SQL Server reports the deadlock victim as
/// native error @c 1205 under a generic SQLSTATE; SQLite reports a busy database as a message
/// string with no usable SQLSTATE at all. A single hard-coded predicate cannot be right for all
/// three, which is why this is a dispatch point rather than a free function.
///
/// Callers normally reach a classifier through @ref SqlRetryPolicy rather than using it directly;
/// this type is the extension point for adding a new dialect.
class [[nodiscard]] LIGHTWEIGHT_API SqlRetryClassifier
{
  public:
    SqlRetryClassifier() = default;
    /// Polymorphic destructor.
    virtual ~SqlRetryClassifier() = default;

    SqlRetryClassifier(SqlRetryClassifier const&) = delete;
    SqlRetryClassifier& operator=(SqlRetryClassifier const&) = delete;
    SqlRetryClassifier(SqlRetryClassifier&&) = delete;
    SqlRetryClassifier& operator=(SqlRetryClassifier&&) = delete;

    /// Classifies the given error.
    ///
    /// Implementations must be pure: no I/O, no handle access, no hidden state. That is what lets
    /// a test drive every branch by constructing a @ref SqlErrorInfo, rather than having to
    /// provoke a real driver failure.
    ///
    /// @param error The error reported by the failed attempt.
    /// @return Whether another attempt could plausibly succeed.
    [[nodiscard]] virtual SqlErrorTransience Classify(SqlErrorInfo const& error) const noexcept = 0;

    /// Convenience predicate over @ref Classify.
    ///
    /// @param error The error reported by the failed attempt.
    /// @return @c true if @p error is classified as @ref SqlErrorTransience::Transient.
    [[nodiscard]] bool IsTransient(SqlErrorInfo const& error) const noexcept
    {
        return Classify(error) == SqlErrorTransience::Transient;
    }
};

/// @ingroup Retry
/// @brief Returns the dialect-agnostic classifier.
///
/// Recognises the transient SQLSTATE classes every ODBC driver shares (connection class @c 08,
/// transaction-rollback class @c 40, and the @c HYT00 / @c HYT01 timeouts) plus the widely
/// observed native codes and driver messages. It is the fallback used when the dialect is not
/// known — for instance when no connection is at hand, or for a server type that has no
/// formatter of its own.
[[nodiscard]] LIGHTWEIGHT_API SqlRetryClassifier const& GenericRetryOps() noexcept;

/// @ingroup Retry
/// @brief Returns the SQLite-specific singleton classifier.
///
/// Defined in `SqlRetryPolicy.cpp` for the same reason the advisory-lock handlers are defined out
/// of line — the formatter overrides delegate to these free functions inline, which keeps every
/// formatter's vtable weak. See @c SqliteAdvisoryLockOps().
[[nodiscard]] LIGHTWEIGHT_API SqlRetryClassifier const& SqliteRetryOps() noexcept;

/// @ingroup Retry
/// @brief Returns the SQL Server-specific singleton classifier. See @ref SqliteRetryOps().
[[nodiscard]] LIGHTWEIGHT_API SqlRetryClassifier const& SqlServerRetryOps() noexcept;

/// @ingroup Retry
/// @brief Returns the PostgreSQL-specific singleton classifier. See @ref SqliteRetryOps().
[[nodiscard]] LIGHTWEIGHT_API SqlRetryClassifier const& PostgreSqlRetryOps() noexcept;

} // namespace Lightweight
