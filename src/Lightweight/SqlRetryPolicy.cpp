// SPDX-License-Identifier: Apache-2.0

#include "SqlConnection.hpp"
#include "SqlErrorDetection.hpp"
#include "SqlQueryFormatter.hpp"
#include "SqlRetryPolicy.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ranges>
#include <span>
#include <string_view>
#include <thread>

namespace Lightweight
{

namespace
{

    /// SQLSTATE classes that every ODBC driver reports for a temporary condition.
    ///
    /// Kept as a table rather than a chain of comparisons so a new class is one line, and so the
    /// dialect classifiers below can share it verbatim instead of restating it.
    constexpr std::array TransientSqlStateClasses = {
        std::string_view { "08" }, // connection exception — dropped, rejected, or never established
        std::string_view { "40" }, // transaction rollback — serialization failure, deadlock, integrity
    };

    /// Complete SQLSTATEs that are transient but do not belong to a wholly transient class.
    constexpr std::array TransientSqlStates = {
        std::string_view { "HYT00" }, // timeout expired
        std::string_view { "HYT01" }, // connection timeout expired
    };

    /// Whether the SQLSTATE alone is enough to call the error transient, in any dialect.
    [[nodiscard]] bool IsTransientSqlState(std::string_view state) noexcept
    {
        return std::ranges::any_of(TransientSqlStateClasses,
                                   [state](std::string_view prefix) { return state.starts_with(prefix); })
               || std::ranges::contains(TransientSqlStates, state);
    }

    /// Whether any of @p needles occurs in @p haystack.
    [[nodiscard]] bool ContainsAny(std::string_view haystack, std::span<std::string_view const> needles) noexcept
    {
        return std::ranges::any_of(needles, [haystack](std::string_view needle) { return haystack.contains(needle); });
    }

    /// Dialect-agnostic fallback. Deliberately the union of what the supported drivers report, so
    /// that classifying without knowing the dialect errs towards retrying rather than towards
    /// giving up on a recoverable failure.
    ///
    /// Shares its body with `Lightweight::IsTransientError` so the project has exactly one
    /// definition of the dialect-agnostic heuristics.
    class GenericRetryClassifier final: public SqlRetryClassifier
    {
      public:
        [[nodiscard]] SqlErrorTransience Classify(SqlErrorInfo const& error) const noexcept override
        {
            return IsTransientError(error) ? SqlErrorTransience::Transient : SqlErrorTransience::Permanent;
        }
    };

    /// SQLite reports contention through the driver's message text: the ODBC layer maps
    /// `SQLITE_BUSY` and `SQLITE_LOCKED` onto the generic `HY000`, so the SQLSTATE carries no
    /// usable signal and the message is all there is to go on.
    class SqliteRetryClassifier final: public SqlRetryClassifier
    {
      public:
        [[nodiscard]] SqlErrorTransience Classify(SqlErrorInfo const& error) const noexcept override
        {
            static constexpr std::array Needles = {
                std::string_view { "database is locked" },
                std::string_view { "database table is locked" },
                std::string_view { "SQLITE_BUSY" },
                std::string_view { "SQLITE_LOCKED" },
            };

            if (IsTransientSqlState(error.sqlState) || ContainsAny(error.message, Needles))
                return SqlErrorTransience::Transient;

            return SqlErrorTransience::Permanent;
        }
    };

    /// SQL Server signals most recoverable conditions through the native error code while leaving
    /// the SQLSTATE generic, so the native code is the discriminator here.
    class SqlServerRetryClassifier final: public SqlRetryClassifier
    {
      public:
        [[nodiscard]] SqlErrorTransience Classify(SqlErrorInfo const& error) const noexcept override
        {
            static constexpr std::array<SQLINTEGER, 12> TransientNativeCodes = {
                -2,    // query timeout expired
                64,    // session terminated: a transport-level error during login
                233,   // no process on the other end of the pipe
                1205,  // chosen as the deadlock victim
                1222,  // lock request timed out
                10053, // transport-level error while sending the request
                10054, // connection forcibly closed by the remote host
                10060, // could not open a connection: network or instance-specific error
                40197, // Azure SQL: the service encountered an error processing the request
                40501, // Azure SQL: the service is busy
                40613, // Azure SQL: the database is currently unavailable
                49918, // Azure SQL: cannot process the request, not enough resources
            };

            if (IsTransientSqlState(error.sqlState) || std::ranges::contains(TransientNativeCodes, error.nativeErrorCode))
                return SqlErrorTransience::Transient;

            return SqlErrorTransience::Permanent;
        }
    };

    /// PostgreSQL is the most precise of the three: it has a dedicated SQLSTATE for every
    /// recoverable condition, so no message matching is needed.
    class PostgreSqlRetryClassifier final: public SqlRetryClassifier
    {
      public:
        [[nodiscard]] SqlErrorTransience Classify(SqlErrorInfo const& error) const noexcept override
        {
            // 40001 serialization_failure and 40P01 deadlock_detected are already covered by the
            // shared class-40 prefix; what follows are the ones outside a transient class.
            static constexpr std::array ExtraTransientStates = {
                std::string_view { "53300" }, // too_many_connections
                std::string_view { "55006" }, // object_in_use
                std::string_view { "55P03" }, // lock_not_available
                std::string_view { "57P01" }, // admin_shutdown
                std::string_view { "57P02" }, // crash_shutdown
                std::string_view { "57P03" }, // cannot_connect_now — the server is still starting up
                std::string_view { "58030" }, // io_error
            };

            if (IsTransientSqlState(error.sqlState) || std::ranges::contains(ExtraTransientStates, error.sqlState))
                return SqlErrorTransience::Transient;

            return SqlErrorTransience::Permanent;
        }
    };

    class ThisThreadSleeper final: public SqlRetrySleeper
    {
      public:
        void Sleep(std::chrono::milliseconds duration) override
        {
            std::this_thread::sleep_for(duration);
        }
    };

} // namespace

SqlRetryClassifier const& GenericRetryOps() noexcept
{
    static GenericRetryClassifier const instance;
    return instance;
}

SqlRetryClassifier const& SqliteRetryOps() noexcept
{
    static SqliteRetryClassifier const instance;
    return instance;
}

SqlRetryClassifier const& SqlServerRetryOps() noexcept
{
    static SqlServerRetryClassifier const instance;
    return instance;
}

SqlRetryClassifier const& PostgreSqlRetryOps() noexcept
{
    static PostgreSqlRetryClassifier const instance;
    return instance;
}

SqlRetrySleeper& ThreadSleeper() noexcept
{
    static ThisThreadSleeper instance;
    return instance;
}

SqlRetryPolicy::SqlRetryPolicy(SqlRetrySettings settings,
                               SqlRetryClassifier const* classifier,
                               SqlRetrySleeper* sleeper,
                               RetryObserver observer):
    _settings { settings },
    _classifier { classifier ? classifier : &GenericRetryOps() },
    _sleeper { sleeper ? sleeper : &ThreadSleeper() },
    _observer { std::move(observer) }
{
}

SqlRetryPolicy SqlRetryPolicy::For(SqlServerType serverType, SqlRetrySettings settings)
{
    auto const* formatter = SqlQueryFormatter::Get(serverType);
    return SqlRetryPolicy { settings, formatter ? &formatter->RetryOps() : &GenericRetryOps() };
}

SqlRetryPolicy SqlRetryPolicy::For(SqlConnection const& connection, SqlRetrySettings settings)
{
    return SqlRetryPolicy { settings, &connection.QueryFormatter().RetryOps() };
}

std::chrono::milliseconds SqlRetryPolicy::DelayFor(unsigned retryIndex) const noexcept
{
    auto delay = _settings.initialDelay;

    for ([[maybe_unused]] auto const step: std::views::iota(0U, retryIndex))
    {
        // Bail out once the cap is reached: the result is clamped below anyway, and stopping here
        // keeps the multiplication from overflowing for an implausibly large retry index. Only
        // safe while the sequence is non-decreasing, hence the multiplier guard.
        if (_settings.backoffMultiplier >= 1.0 && delay >= _settings.maxDelay)
            break;

        delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double, std::milli>(static_cast<double>(delay.count()) * _settings.backoffMultiplier));
    }

    return std::min(delay, _settings.maxDelay);
}

SqlRetryDecision SqlRetryPolicy::Decide(SqlErrorInfo const& error, SqlRetryState const& state) const noexcept
{
    if (_classifier->Classify(error) != SqlErrorTransience::Transient)
        return { .action = SqlRetryAction::GiveUp, .delay = {}, .reason = SqlRetryGiveUpReason::NotTransient };

    if (state.retriesSoFar >= _settings.maxRetries)
        return { .action = SqlRetryAction::GiveUp, .delay = {}, .reason = SqlRetryGiveUpReason::RetriesExhausted };

    auto const delay = DelayFor(state.retriesSoFar);

    if (_settings.totalDelayBudget > std::chrono::milliseconds { 0 }
        && state.delaySoFar + delay > _settings.totalDelayBudget)
        return { .action = SqlRetryAction::GiveUp, .delay = {}, .reason = SqlRetryGiveUpReason::DelayBudgetExhausted };

    return { .action = SqlRetryAction::Retry, .delay = delay, .reason = SqlRetryGiveUpReason::None };
}

void SqlRetryPolicy::NotifyRetry(SqlRetryAttempt const& attempt) const
{
    if (_observer)
        _observer(attempt);
}

} // namespace Lightweight
