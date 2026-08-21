// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlRetryClassifier.hpp>
#include <Lightweight/SqlRetryPolicy.hpp>
#include <Lightweight/SqlServerType.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace Lightweight;
using namespace std::chrono_literals;

// The whole policy is deliberately free of I/O and of any clock, so every case below runs without
// a database: an error is a value, the sleeper is injected, and the decision is a pure function of
// its inputs. That is what makes the per-DBMS classification testable at all — provoking a real
// Azure SQL throttle or a PostgreSQL serialization failure on demand is not something a unit test
// can do.

namespace
{

SqlErrorInfo MakeError(std::string sqlState, SQLINTEGER nativeCode = 0, std::string message = {})
{
    return SqlErrorInfo {
        .nativeErrorCode = nativeCode,
        .sqlState = std::move(sqlState),
        .message = std::move(message),
    };
}

/// Records what it was asked to wait for instead of actually waiting, so a full retry loop
/// finishes in microseconds and the backoff schedule is directly assertable.
class RecordingSleeper final: public SqlRetrySleeper
{
  public:
    void Sleep(std::chrono::milliseconds duration) override
    {
        slept.emplace_back(duration);
    }

    std::vector<std::chrono::milliseconds> slept;
};

/// Fails with the given error for the first `failures` calls, then succeeds.
class FlakyOperation
{
  public:
    FlakyOperation(unsigned failures, SqlErrorInfo error):
        _remainingFailures { failures },
        _error { std::move(error) }
    {
    }

    int operator()()
    {
        ++calls;
        if (_remainingFailures > 0)
        {
            --_remainingFailures;
            throw SqlException { _error };
        }
        return 42;
    }

    unsigned calls = 0;

  private:
    unsigned _remainingFailures;
    SqlErrorInfo _error;
};

/// Backoff collapsed to a single millisecond, for cases about control flow rather than timing.
constexpr auto FastSettings = SqlRetrySettings {
    .maxRetries = 3,
    .initialDelay = 1ms,
    .backoffMultiplier = 1.0,
    .maxDelay = 1ms,
};

} // namespace

// ================================================================================================
// Per-DBMS classification — the core of the issue: a transient error in one dialect is a plain
// failure in another, so each classifier must recognise its own dialect and only its own.
// ================================================================================================

TEST_CASE("Every classifier accepts the shared ODBC transient SQLSTATE classes", "[SqlRetryPolicy]")
{
    // Class 08 (connection), class 40 (transaction rollback) and the HYT timeouts are ODBC-level
    // and therefore dialect-independent — no classifier may disagree about them.
    for (auto const* classifier: { &GenericRetryOps(), &SqliteRetryOps(), &SqlServerRetryOps(), &PostgreSqlRetryOps() })
    {
        CHECK(classifier->IsTransient(MakeError("08001"))); // unable to connect
        CHECK(classifier->IsTransient(MakeError("08S01"))); // communication link failure
        CHECK(classifier->IsTransient(MakeError("40001"))); // serialization failure
        CHECK(classifier->IsTransient(MakeError("HYT00"))); // timeout expired
        CHECK(classifier->IsTransient(MakeError("HYT01"))); // connection timeout expired

        CHECK_FALSE(classifier->IsTransient(MakeError("23505"))); // unique violation
        CHECK_FALSE(classifier->IsTransient(MakeError("42S02"))); // table not found
        CHECK_FALSE(classifier->IsTransient(MakeError("42000"))); // syntax error
    }
}

TEST_CASE("SQL Server classifier keys off the native error code", "[SqlRetryPolicy]")
{
    auto const& classifier = SqlServerRetryOps();

    // SQL Server reports these under a generic SQLSTATE, so the native code is the only signal.
    CHECK(classifier.IsTransient(MakeError("HY000", 1205)));  // deadlock victim
    CHECK(classifier.IsTransient(MakeError("HY000", 1222)));  // lock request timed out
    CHECK(classifier.IsTransient(MakeError("HY000", -2)));    // query timeout
    CHECK(classifier.IsTransient(MakeError("HY000", 10054))); // connection forcibly closed
    CHECK(classifier.IsTransient(MakeError("HY000", 40501))); // Azure SQL: service is busy
    CHECK(classifier.IsTransient(MakeError("HY000", 40613))); // Azure SQL: database unavailable

    CHECK_FALSE(classifier.IsTransient(MakeError("HY000", 2627))); // unique key violation
    CHECK_FALSE(classifier.IsTransient(MakeError("HY000", 547)));  // FK constraint conflict
    CHECK_FALSE(classifier.IsTransient(MakeError("HY000", 208)));  // invalid object name
}

TEST_CASE("PostgreSQL classifier keys off dedicated SQLSTATEs", "[SqlRetryPolicy]")
{
    auto const& classifier = PostgreSqlRetryOps();

    CHECK(classifier.IsTransient(MakeError("40P01"))); // deadlock_detected
    CHECK(classifier.IsTransient(MakeError("55P03"))); // lock_not_available
    CHECK(classifier.IsTransient(MakeError("57P01"))); // admin_shutdown
    CHECK(classifier.IsTransient(MakeError("57P03"))); // cannot_connect_now
    CHECK(classifier.IsTransient(MakeError("53300"))); // too_many_connections

    CHECK_FALSE(classifier.IsTransient(MakeError("23503"))); // foreign_key_violation
    CHECK_FALSE(classifier.IsTransient(MakeError("42P01"))); // undefined_table

    // A SQL Server native code carries no meaning for PostgreSQL: the dialects must not bleed.
    CHECK_FALSE(classifier.IsTransient(MakeError("HY000", 1205)));
}

TEST_CASE("SQLite classifier keys off the driver message", "[SqlRetryPolicy]")
{
    auto const& classifier = SqliteRetryOps();

    // The SQLite ODBC driver flattens busy/locked onto HY000, leaving only the message text.
    CHECK(classifier.IsTransient(MakeError("HY000", 5, "database is locked")));
    CHECK(classifier.IsTransient(MakeError("HY000", 6, "database table is locked")));
    CHECK(classifier.IsTransient(MakeError("HY000", 5, "driver reported SQLITE_BUSY")));
    CHECK(classifier.IsTransient(MakeError("HY000", 6, "driver reported SQLITE_LOCKED")));

    CHECK_FALSE(classifier.IsTransient(MakeError("HY000", 1, "no such table: orders")));
    CHECK_FALSE(classifier.IsTransient(MakeError("HY000", 19, "UNIQUE constraint failed: t.id")));
}

TEST_CASE("SqlRetryPolicy::For maps a server type to its dialect classifier", "[SqlRetryPolicy]")
{
    // Dispatch runs through SqlQueryFormatter::Get(), so this also pins down that the formatter
    // for each supported DBMS really does return its own classifier.
    CHECK(&SqlRetryPolicy::For(SqlServerType::SQLITE).Classifier() == &SqliteRetryOps());
    CHECK(&SqlRetryPolicy::For(SqlServerType::MICROSOFT_SQL).Classifier() == &SqlServerRetryOps());
    CHECK(&SqlRetryPolicy::For(SqlServerType::POSTGRESQL).Classifier() == &PostgreSqlRetryOps());

    // A server type with no formatter of its own must still yield a usable policy.
    CHECK(&SqlRetryPolicy::For(SqlServerType::UNKNOWN).Classifier() == &GenericRetryOps());
    CHECK(&SqlRetryPolicy::For(SqlServerType::MYSQL).Classifier() == &GenericRetryOps());
}

TEST_CASE("SqlRetryPolicy::For carries the settings through", "[SqlRetryPolicy]")
{
    auto const policy = SqlRetryPolicy::For(SqlServerType::POSTGRESQL, SqlRetrySettings { .maxRetries = 9 });
    CHECK(policy.Settings().maxRetries == 9);
}

// ================================================================================================
// DelayFor — exponential backoff with a cap
// ================================================================================================

TEST_CASE("DelayFor grows exponentially and clamps at maxDelay", "[SqlRetryPolicy]")
{
    auto const policy = SqlRetryPolicy { SqlRetrySettings {
        .maxRetries = 10, .initialDelay = 100ms, .backoffMultiplier = 2.0, .maxDelay = 5000ms } };

    CHECK(policy.DelayFor(0) == 100ms);
    CHECK(policy.DelayFor(1) == 200ms);
    CHECK(policy.DelayFor(2) == 400ms);
    CHECK(policy.DelayFor(3) == 800ms);

    // 100 * 2^6 = 6400ms would overshoot the 5s cap.
    CHECK(policy.DelayFor(6) == 5000ms);

    // A retry index far past the cap must stay clamped rather than overflow.
    CHECK(policy.DelayFor(1000) == 5000ms);
}

TEST_CASE("DelayFor handles a fractional multiplier", "[SqlRetryPolicy]")
{
    auto const policy = SqlRetryPolicy { SqlRetrySettings {
        .maxRetries = 5, .initialDelay = 100ms, .backoffMultiplier = 1.5, .maxDelay = 10'000ms } };

    CHECK(policy.DelayFor(1) == 150ms);
    CHECK(policy.DelayFor(2) == 225ms);
}

TEST_CASE("DelayFor with a multiplier of one is a constant delay", "[SqlRetryPolicy]")
{
    auto const policy = SqlRetryPolicy { SqlRetrySettings {
        .maxRetries = 5, .initialDelay = 250ms, .backoffMultiplier = 1.0, .maxDelay = 10'000ms } };

    CHECK(policy.DelayFor(0) == 250ms);
    CHECK(policy.DelayFor(4) == 250ms);
}

// ================================================================================================
// Decide — the pure decision, including why it gave up
// ================================================================================================

TEST_CASE("Decide retries a transient error while budget remains", "[SqlRetryPolicy]")
{
    auto const policy = SqlRetryPolicy { SqlRetrySettings { .maxRetries = 3, .initialDelay = 100ms } };

    auto const decision = policy.Decide(MakeError("40001"), SqlRetryState { .retriesSoFar = 0 });
    CHECK(decision.action == SqlRetryAction::Retry);
    CHECK(decision.reason == SqlRetryGiveUpReason::None);
    CHECK(decision.delay == 100ms);
    CHECK(static_cast<bool>(decision));
}

TEST_CASE("Decide reports why it gave up", "[SqlRetryPolicy]")
{
    auto const policy = SqlRetryPolicy { SqlRetrySettings { .maxRetries = 3 } };

    SECTION("a permanent error is never retried, however much budget is left")
    {
        auto const decision = policy.Decide(MakeError("23505"), SqlRetryState { .retriesSoFar = 0 });
        CHECK(decision.action == SqlRetryAction::GiveUp);
        CHECK(decision.reason == SqlRetryGiveUpReason::NotTransient);
        CHECK(decision.delay == 0ms);
        CHECK_FALSE(static_cast<bool>(decision));
    }

    SECTION("a transient error stops once the retry budget is spent")
    {
        // retriesSoFar == maxRetries is the boundary — the budget is gone.
        auto const decision = policy.Decide(MakeError("08S01"), SqlRetryState { .retriesSoFar = 3 });
        CHECK(decision.action == SqlRetryAction::GiveUp);
        CHECK(decision.reason == SqlRetryGiveUpReason::RetriesExhausted);
    }
}

TEST_CASE("Decide honours a zero retry budget", "[SqlRetryPolicy]")
{
    auto const policy = SqlRetryPolicy { SqlRetrySettings { .maxRetries = 0 } };

    auto const decision = policy.Decide(MakeError("08S01"), SqlRetryState {});
    CHECK(decision.action == SqlRetryAction::GiveUp);
    CHECK(decision.reason == SqlRetryGiveUpReason::RetriesExhausted);
}

TEST_CASE("Decide stops once the cumulative delay budget would be overrun", "[SqlRetryPolicy]")
{
    // The deadline half of the issue: retries must stop when the time budget is gone, even though
    // the retry count still allows more.
    auto const policy = SqlRetryPolicy { SqlRetrySettings { .maxRetries = 10,
                                                            .initialDelay = 100ms,
                                                            .backoffMultiplier = 2.0,
                                                            .maxDelay = 10'000ms,
                                                            .totalDelayBudget = 500ms } };

    // 300ms spent + a 400ms next delay = 700ms > 500ms budget.
    auto const overrun = policy.Decide(MakeError("40001"), SqlRetryState { .retriesSoFar = 2, .delaySoFar = 300ms });
    CHECK(overrun.action == SqlRetryAction::GiveUp);
    CHECK(overrun.reason == SqlRetryGiveUpReason::DelayBudgetExhausted);

    // 100ms spent + a 200ms next delay = 300ms, still inside the budget.
    auto const withinBudget = policy.Decide(MakeError("40001"), SqlRetryState { .retriesSoFar = 1, .delaySoFar = 100ms });
    CHECK(withinBudget.action == SqlRetryAction::Retry);
}

TEST_CASE("A zero delay budget means no deadline", "[SqlRetryPolicy]")
{
    auto const policy =
        SqlRetryPolicy { SqlRetrySettings { .maxRetries = 10, .initialDelay = 1000ms, .totalDelayBudget = 0ms } };

    auto const decision = policy.Decide(MakeError("40001"), SqlRetryState { .retriesSoFar = 5, .delaySoFar = 60'000ms });
    CHECK(decision.action == SqlRetryAction::Retry);
}

TEST_CASE("Decide consults the injected classifier, not a hardcoded predicate", "[SqlRetryPolicy]")
{
    auto const settings = SqlRetrySettings { .maxRetries = 3 };
    auto const sqlServerPolicy = SqlRetryPolicy { settings, &SqlServerRetryOps() };
    auto const postgresPolicy = SqlRetryPolicy { settings, &PostgreSqlRetryOps() };

    // Native error 1205 is a SQL Server deadlock victim and nothing at all to PostgreSQL.
    auto const deadlock = MakeError("HY000", 1205);
    CHECK(sqlServerPolicy.Decide(deadlock, SqlRetryState {}).action == SqlRetryAction::Retry);
    CHECK(postgresPolicy.Decide(deadlock, SqlRetryState {}).action == SqlRetryAction::GiveUp);
}

// ================================================================================================
// Execute / TryExecute — the driver over Decide
// ================================================================================================

TEST_CASE("Execute retries a transient failure and returns the eventual result", "[SqlRetryPolicy]")
{
    auto sleeper = RecordingSleeper {};
    auto const policy = SqlRetryPolicy {
        SqlRetrySettings { .maxRetries = 3, .initialDelay = 100ms, .backoffMultiplier = 2.0, .maxDelay = 10'000ms },
        &GenericRetryOps(),
        &sleeper
    };

    auto operation = FlakyOperation { 2, MakeError("40001") };
    auto const result = policy.Execute(std::ref(operation));

    CHECK(result == 42);
    CHECK(operation.calls == 3); // two failures plus the successful attempt
    REQUIRE(sleeper.slept.size() == 2);
    CHECK(sleeper.slept[0] == 100ms);
    CHECK(sleeper.slept[1] == 200ms);
}

TEST_CASE("Execute does not retry a permanent failure", "[SqlRetryPolicy]")
{
    auto sleeper = RecordingSleeper {};
    auto const policy = SqlRetryPolicy { FastSettings, &GenericRetryOps(), &sleeper };

    auto operation = FlakyOperation { 1, MakeError("23505") };
    CHECK_THROWS_AS(policy.Execute(std::ref(operation)), SqlException);

    CHECK(operation.calls == 1);
    CHECK(sleeper.slept.empty());
}

TEST_CASE("Execute rethrows the original error once the budget is spent", "[SqlRetryPolicy]")
{
    auto sleeper = RecordingSleeper {};
    auto const policy = SqlRetryPolicy { FastSettings, &GenericRetryOps(), &sleeper };

    // Always fails: 1 initial attempt + 3 retries, then the last SqlException escapes unwrapped.
    auto operation = FlakyOperation { 99, MakeError("08S01", 0, "link failure") };
    try
    {
        (void) policy.Execute(std::ref(operation));
        FAIL("expected the exhausted policy to rethrow");
    }
    catch (SqlException const& e)
    {
        CHECK(e.info().sqlState == "08S01");
        CHECK(e.info().message == "link failure");
    }

    CHECK(operation.calls == 4);
    CHECK(sleeper.slept.size() == 3);
}

TEST_CASE("Execute lets a non-SQL exception through untouched", "[SqlRetryPolicy]")
{
    auto sleeper = RecordingSleeper {};
    auto const policy = SqlRetryPolicy { FastSettings, &GenericRetryOps(), &sleeper };

    CHECK_THROWS_AS(policy.Execute([] -> int { throw std::runtime_error { "not a SQL error" }; }), std::runtime_error);
    CHECK(sleeper.slept.empty());
}

TEST_CASE("Execute supports a void-returning operation", "[SqlRetryPolicy]")
{
    auto sleeper = RecordingSleeper {};
    auto const policy = SqlRetryPolicy { FastSettings, &GenericRetryOps(), &sleeper };

    unsigned calls = 0;
    policy.Execute([&calls] {
        ++calls;
        if (calls == 1)
            throw SqlException { MakeError("40001") };
    });

    CHECK(calls == 2);
}

TEST_CASE("TryExecute reports a final failure as unexpected", "[SqlRetryPolicy]")
{
    auto sleeper = RecordingSleeper {};
    auto const policy = SqlRetryPolicy { FastSettings, &GenericRetryOps(), &sleeper };

    SECTION("success carries the value")
    {
        auto operation = FlakyOperation { 1, MakeError("40001") };
        auto const result = policy.TryExecute(std::ref(operation));
        REQUIRE(result.has_value());
        CHECK(*result == 42);
    }

    SECTION("a permanent failure carries the error info")
    {
        auto operation = FlakyOperation { 1, MakeError("23505", 0, "duplicate key") };
        auto const result = policy.TryExecute(std::ref(operation));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().sqlState == "23505");
        CHECK(result.error().message == "duplicate key");
    }

    SECTION("a void operation yields an empty expected")
    {
        auto const result = policy.TryExecute([] {});
        CHECK(result.has_value());
    }
}

TEST_CASE("The retry observer sees every retry before the wait", "[SqlRetryPolicy]")
{
    auto sleeper = RecordingSleeper {};
    auto policy = SqlRetryPolicy {
        SqlRetrySettings { .maxRetries = 3, .initialDelay = 100ms, .backoffMultiplier = 2.0, .maxDelay = 10'000ms },
        &GenericRetryOps(),
        &sleeper
    };

    auto seen = std::vector<SqlRetryAttempt> {};
    policy.SetRetryObserver([&seen](SqlRetryAttempt const& attempt) { seen.emplace_back(attempt); });

    auto operation = FlakyOperation { 2, MakeError("HYT00", 0, "timeout expired") };
    (void) policy.Execute(std::ref(operation));

    REQUIRE(seen.size() == 2);
    CHECK(seen[0].retryNumber == 1);
    CHECK(seen[0].maxRetries == 3);
    CHECK(seen[0].delay == 100ms);
    CHECK(seen[0].error.sqlState == "HYT00");
    CHECK(seen[1].retryNumber == 2);
    CHECK(seen[1].delay == 200ms);
}

TEST_CASE("A default-constructed policy is usable", "[SqlRetryPolicy]")
{
    auto const policy = SqlRetryPolicy {};

    CHECK(policy.Settings().maxRetries == 3);
    CHECK(policy.Settings().initialDelay == 500ms);
    CHECK(policy.Settings().backoffMultiplier == 2.0);
    CHECK(policy.Settings().maxDelay == 30'000ms);
    CHECK(policy.Settings().totalDelayBudget == 0ms);
    CHECK(&policy.Classifier() == &GenericRetryOps());

    // No sleeping happens on the success path, so this stays fast despite the real sleeper.
    CHECK(policy.Execute([] { return 7; }) == 7);
}

// ================================================================================================
// The SqlConnection overload, exercised against whichever server the suite is pointed at. This is
// the one part of the policy that cannot be checked without a database: it asserts that a live
// connection resolves to the classifier for its own dialect.
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlRetryPolicy::For(connection) picks the live server's classifier",
                 "[SqlRetryPolicy]")
{
    auto stmt = SqlStatement {};
    auto const& connection = stmt.Connection();

    auto const policy = SqlRetryPolicy::For(connection, SqlRetrySettings { .maxRetries = 5 });

    CHECK(policy.Settings().maxRetries == 5);
    CHECK(&policy.Classifier() == &SqlRetryPolicy::For(connection.ServerType()).Classifier());

    // Whatever the dialect, the shared ODBC classes must be retryable and a constraint violation
    // must not be — the invariant every backend has to satisfy.
    CHECK(policy.Classifier().IsTransient(MakeError("08S01")));
    CHECK_FALSE(policy.Classifier().IsTransient(MakeError("23505")));
}
