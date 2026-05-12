// SPDX-License-Identifier: Apache-2.0

#include "QueryFormatter/PostgreSqlFormatter.hpp"
#include "QueryFormatter/SQLiteFormatter.hpp"
#include "QueryFormatter/SqlServerFormatter.hpp"
#include "SqlAdvisoryLock.hpp"
#include "SqlConnection.hpp"
#include "SqlQueryFormatter.hpp"
#include "SqlStatement.hpp"

#include <reflection-cpp/reflection.hpp>

using namespace std::string_view_literals;

namespace Lightweight
{

std::string SqlQueryFormatter::FormatTableName(std::string_view schema, std::string_view table)
{
    if (schema.empty())
        return std::format(R"("{}")", table);
    return std::format(R"("{}"."{}")", schema, table);
}

SqlQueryFormatter const& SqlQueryFormatter::Sqlite()
{
    static SQLiteQueryFormatter const formatter {};
    return formatter;
}

SqlQueryFormatter const& SqlQueryFormatter::SqlServer()
{
    static SqlServerQueryFormatter const formatter {};
    return formatter;
}

SqlQueryFormatter const& SqlQueryFormatter::PostgrSQL()
{
    static PostgreSqlFormatter const formatter {};
    return formatter;
}

SqlQueryFormatter const* SqlQueryFormatter::Get(SqlServerType serverType) noexcept
{
    static std::array<SqlQueryFormatter const*, 5> const formatters = {
        nullptr, &SqlQueryFormatter::SqlServer(), &SqlQueryFormatter::PostgrSQL(), &SqlQueryFormatter::Sqlite(),
        nullptr, // MySQL
    };
    return formatters[static_cast<size_t>(serverType)];
}

namespace
{

    /// Internal table name for SQLite's lock-table implementation. Exposed
    /// to tooling (e.g. `dbtool hard-reset`) via
    /// `SqlAdvisoryLockHandler::BookkeepingTableNames()` so the table is
    /// recognised as infrastructure rather than mistaken for user data.
    constexpr std::string_view kSqliteLockTableName = "_lightweight_locks";

    /// SQLite advisory-lock implementation: maintain a `_lightweight_locks`
    /// table whose `lock_name` column has a UNIQUE / PRIMARY KEY constraint.
    /// Acquiring is an `INSERT`; a duplicate insert throws and is mapped to a
    /// `Timeout`. `PRAGMA busy_timeout` ensures the insert blocks (up to
    /// `timeout`) when the database file is held by another connection.
    class SqliteAdvisoryLockHandler final: public SqlAdvisoryLockHandler
    {
      public:
        [[nodiscard]] std::expected<void, SqlLockError> TryAcquire(SqlConnection& connection,
                                                                   std::string_view lockName,
                                                                   std::chrono::milliseconds timeout) const override
        {
            try
            {
                auto stmt = SqlStatement { connection };

                [[maybe_unused]] auto busyCursor =
                    stmt.ExecuteDirect(std::format("PRAGMA busy_timeout = {};", timeout.count()));

                [[maybe_unused]] auto createCursor =
                    stmt.ExecuteDirect(std::format(R"(CREATE TABLE IF NOT EXISTS "{}" ()"
                                                   R"("lock_name" VARCHAR(255) PRIMARY KEY, )"
                                                   R"("acquired_at" TEXT DEFAULT CURRENT_TIMESTAMP);)",
                                                   kSqliteLockTableName));

                [[maybe_unused]] auto insertCursor = stmt.ExecuteDirect(
                    std::format(R"(INSERT INTO "{}" ("lock_name") VALUES ('{}');)", kSqliteLockTableName, lockName));
                return {};
            }
            catch (SqlException const& ex)
            {
                // Duplicate-key collision is the SQLite signal for "lock is held".
                // We can't always tell from SQLSTATE whether it was a busy_timeout
                // or a constraint violation, so we report `Timeout` in either case
                // (both mean "couldn't acquire within budget").
                return std::unexpected(SqlLockError {
                    .reason = SqlLockFailureReason::Timeout,
                    .lockName = std::string { lockName },
                    .timeout = timeout,
                    .message = std::format("SqlScopedLock: SQLite database is locked, "
                                           "could not acquire lock '{}' within {} ms",
                                           lockName,
                                           timeout.count()),
                    .info = ex.info(),
                });
            }
        }

        [[nodiscard]] std::expected<void, SqlLockError> Release(SqlConnection& connection,
                                                                std::string_view lockName) const override
        {
            try
            {
                auto stmt = SqlStatement { connection };
                [[maybe_unused]] auto deleteCursor = stmt.ExecuteDirect(
                    std::format(R"(DELETE FROM "{}" WHERE "lock_name" = '{}';)", kSqliteLockTableName, lockName));
                return {};
            }
            catch (SqlException const& ex)
            {
                // Idempotent-Release contract: "lock that's already gone" is success.
                // After `dbtool hard-reset` drops `_lightweight_locks`, the holder
                // process's destructor still tries to release. The table is gone,
                // which means the lock is gone too — exactly the case the contract
                // says must be a no-op.
                if (ex.info().message.contains("no such table"))
                    return {};
                return std::unexpected(SqlLockError {
                    .reason = SqlLockFailureReason::DriverError,
                    .lockName = std::string { lockName },
                    .timeout = {},
                    .message = std::format("SqlScopedLock: SQLite release of '{}' failed: {}", lockName, ex.info().message),
                    .info = ex.info(),
                });
            }
        }

        [[nodiscard]] std::vector<std::string_view> BookkeepingTableNames() const noexcept override
        {
            return { kSqliteLockTableName };
        }
    };

    /// SQL Server advisory-lock implementation: `sp_getapplock` /
    /// `sp_releaseapplock` with `@LockOwner=N'Session'`. The acquire stored
    /// procedure returns a result code:
    ///   0   granted synchronously
    ///   1   granted after waiting
    ///  -1   timeout
    ///  -2   request cancelled
    ///  -3   deadlock victim
    /// -999  parameter validation / call error
    /// We map each negative code to a distinct `SqlLockFailureReason` so
    /// callers can decide whether to retry, escalate, or surface to a human.
    class SqlServerAdvisoryLockHandler final: public SqlAdvisoryLockHandler
    {
      public:
        [[nodiscard]] std::expected<void, SqlLockError> TryAcquire(SqlConnection& connection,
                                                                   std::string_view lockName,
                                                                   std::chrono::milliseconds timeout) const override
        {
            auto const sql = std::format("DECLARE @result INT; "
                                         "EXEC @result = sp_getapplock @Resource = N'{}', @LockMode = N'Exclusive', "
                                         "@LockTimeout = {}, @LockOwner = N'Session'; "
                                         "SELECT @result;",
                                         lockName,
                                         timeout.count());

            try
            {
                auto stmt = SqlStatement { connection };
                auto cursor = stmt.ExecuteDirect(sql);

                if (!cursor.FetchRow())
                    return std::unexpected(SqlLockError {
                        .reason = SqlLockFailureReason::DriverError,
                        .lockName = std::string { lockName },
                        .timeout = timeout,
                        .message = std::format("SqlScopedLock: sp_getapplock returned no result row for '{}'", lockName),
                        .info = std::nullopt,
                    });

                auto const result = cursor.GetColumn<int>(1);
                if (result >= 0)
                    return {};

                auto const reason = [&]() -> SqlLockFailureReason {
                    switch (result)
                    {
                        case -1:
                            return SqlLockFailureReason::Timeout;
                        case -2:
                            return SqlLockFailureReason::Cancelled;
                        case -3:
                            return SqlLockFailureReason::Deadlock;
                        case -999:
                            return SqlLockFailureReason::ParameterError;
                        default:
                            return SqlLockFailureReason::DriverError;
                    }
                }();
                auto const reasonText = [&]() -> std::string_view {
                    switch (reason)
                    {
                        case SqlLockFailureReason::Timeout:
                            return "timeout";
                        case SqlLockFailureReason::Cancelled:
                            return "cancelled";
                        case SqlLockFailureReason::Deadlock:
                            return "deadlock victim";
                        case SqlLockFailureReason::ParameterError:
                            return "parameter error";
                        default:
                            return "driver error";
                    }
                }();
                return std::unexpected(SqlLockError {
                    .reason = reason,
                    .lockName = std::string { lockName },
                    .timeout = timeout,
                    .message =
                        std::format("SqlScopedLock: sp_getapplock '{}' failed ({}, code {})", lockName, reasonText, result),
                    .info = std::nullopt,
                });
            }
            catch (SqlException const& ex)
            {
                return std::unexpected(SqlLockError {
                    .reason = SqlLockFailureReason::DriverError,
                    .lockName = std::string { lockName },
                    .timeout = timeout,
                    .message =
                        std::format("SqlScopedLock: SQL Server acquire of '{}' failed: {}", lockName, ex.info().message),
                    .info = ex.info(),
                });
            }
        }

        [[nodiscard]] std::expected<void, SqlLockError> Release(SqlConnection& connection,
                                                                std::string_view lockName) const override
        {
            try
            {
                auto stmt = SqlStatement { connection };
                [[maybe_unused]] auto releaseCursor = stmt.ExecuteDirect(
                    std::format("EXEC sp_releaseapplock @Resource = N'{}', @LockOwner = N'Session';", lockName));
                return {};
            }
            catch (SqlException const& ex)
            {
                return std::unexpected(SqlLockError {
                    .reason = SqlLockFailureReason::DriverError,
                    .lockName = std::string { lockName },
                    .timeout = {},
                    .message =
                        std::format("SqlScopedLock: SQL Server release of '{}' failed: {}", lockName, ex.info().message),
                    .info = ex.info(),
                });
            }
        }
    };

    /// PostgreSQL advisory-lock implementation: `pg_advisory_lock` keyed by
    /// the 64-bit hash of the lock name, preceded by `SET lock_timeout` so a
    /// contended lock fails instead of blocking forever. The timeout signal
    /// is SQLSTATE `55P03` (`lock_not_available`); anything else surfaces as
    /// `DriverError`.
    class PostgreSqlAdvisoryLockHandler final: public SqlAdvisoryLockHandler
    {
      public:
        [[nodiscard]] std::expected<void, SqlLockError> TryAcquire(SqlConnection& connection,
                                                                   std::string_view lockName,
                                                                   std::chrono::milliseconds timeout) const override
        {
            auto stmt = SqlStatement { connection };

            // SET lock_timeout: bound the wait. A separate try/catch so a setup failure
            // surfaces as DriverError and not Timeout.
            try
            {
                [[maybe_unused]] auto timeoutCursor =
                    stmt.ExecuteDirect(std::format("SET lock_timeout = '{} ms';", timeout.count()));
            }
            catch (SqlException const& ex)
            {
                return std::unexpected(SqlLockError {
                    .reason = SqlLockFailureReason::DriverError,
                    .lockName = std::string { lockName },
                    .timeout = timeout,
                    .message = std::format("SqlScopedLock: PostgreSQL SET lock_timeout failed: {}", ex.info().message),
                    .info = ex.info(),
                });
            }

            try
            {
                [[maybe_unused]] auto lockCursor =
                    stmt.ExecuteDirect(std::format("SELECT pg_advisory_lock(hashtext('{}')::bigint);", lockName));
                return {};
            }
            catch (SqlException const& ex)
            {
                // Postgres signals a `lock_timeout` expiry with SQLSTATE 55P03
                // ("lock_not_available"). Anything else (driver disconnected, syntax
                // error, etc.) is a real driver error.
                auto const reason =
                    (ex.info().sqlState == "55P03") ? SqlLockFailureReason::Timeout : SqlLockFailureReason::DriverError;
                return std::unexpected(SqlLockError {
                    .reason = reason,
                    .lockName = std::string { lockName },
                    .timeout = timeout,
                    .message =
                        (reason == SqlLockFailureReason::Timeout)
                            ? std::format(
                                  "SqlScopedLock: PostgreSQL lock '{}' timed out after {} ms", lockName, timeout.count())
                            : std::format(
                                  "SqlScopedLock: PostgreSQL acquire of '{}' failed: {}", lockName, ex.info().message),
                    .info = ex.info(),
                });
            }
        }

        [[nodiscard]] std::expected<void, SqlLockError> Release(SqlConnection& connection,
                                                                std::string_view lockName) const override
        {
            try
            {
                auto stmt = SqlStatement { connection };
                [[maybe_unused]] auto unlockCursor =
                    stmt.ExecuteDirect(std::format("SELECT pg_advisory_unlock(hashtext('{}')::bigint);", lockName));
                return {};
            }
            catch (SqlException const& ex)
            {
                return std::unexpected(SqlLockError {
                    .reason = SqlLockFailureReason::DriverError,
                    .lockName = std::string { lockName },
                    .timeout = {},
                    .message =
                        std::format("SqlScopedLock: PostgreSQL release of '{}' failed: {}", lockName, ex.info().message),
                    .info = ex.info(),
                });
            }
        }
    };

} // namespace

SqlAdvisoryLockHandler const& SqliteAdvisoryLockOps()
{
    static SqliteAdvisoryLockHandler const handler {};
    return handler;
}

SqlAdvisoryLockHandler const& SqlServerAdvisoryLockOps()
{
    static SqlServerAdvisoryLockHandler const handler {};
    return handler;
}

SqlAdvisoryLockHandler const& PostgreSqlAdvisoryLockOps()
{
    static PostgreSqlAdvisoryLockHandler const handler {};
    return handler;
}

} // namespace Lightweight
