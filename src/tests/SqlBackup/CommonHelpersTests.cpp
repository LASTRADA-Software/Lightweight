// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/Common.hpp>
#include <Lightweight/SqlBackup/SqlBackup.hpp>
#include <Lightweight/SqlError.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <regex>
#include <string>
#include <utility>
#include <vector>

using Lightweight::SqlErrorInfo;
using Lightweight::SqlBackup::RetrySettings;
namespace detail = Lightweight::SqlBackup::detail;

namespace
{

SqlErrorInfo MakeError(std::string sqlState, std::string message = {})
{
    return SqlErrorInfo {
        .nativeErrorCode = 0,
        .sqlState = std::move(sqlState),
        .message = std::move(message),
    };
}

} // namespace

// ================================================================================================
// detail::IsTransientError
// ================================================================================================

TEST_CASE("SqlBackup::detail::IsTransientError covers all transient classes", "[SqlBackup]")
{
    CHECK(detail::IsTransientError(MakeError("08001"))); // connection error
    CHECK(detail::IsTransientError(MakeError("08S01"))); // connection error variant
    CHECK(detail::IsTransientError(MakeError("HYT00"))); // timeout
    CHECK(detail::IsTransientError(MakeError("HYT01"))); // connection timeout
    CHECK(detail::IsTransientError(MakeError("40001"))); // serialization failure
    CHECK(detail::IsTransientError(MakeError("HY000", "database is locked")));
    CHECK(detail::IsTransientError(MakeError("HY000", "got SQLITE_BUSY at line 42")));
}

TEST_CASE("SqlBackup::detail::IsTransientError rejects non-transient errors", "[SqlBackup]")
{
    CHECK_FALSE(detail::IsTransientError(MakeError("42S01"))); // table already exists
    CHECK_FALSE(detail::IsTransientError(MakeError("23505"))); // unique violation
    CHECK_FALSE(detail::IsTransientError(MakeError("HY000", "syntax error")));
    CHECK_FALSE(detail::IsTransientError(MakeError("00000"))); // empty / success-ish
}

// ================================================================================================
// detail::CalculateRetryDelay (exponential backoff with cap)
// ================================================================================================

TEST_CASE("CalculateRetryDelay returns initialDelay on attempt 0", "[SqlBackup]")
{
    RetrySettings const settings { .maxRetries = 3,
                                   .initialDelay = std::chrono::milliseconds { 100 },
                                   .backoffMultiplier = 2.0,
                                   .maxDelay = std::chrono::milliseconds { 5000 } };
    CHECK(detail::CalculateRetryDelay(0, settings) == std::chrono::milliseconds { 100 });
}

TEST_CASE("CalculateRetryDelay grows exponentially with the multiplier", "[SqlBackup]")
{
    RetrySettings const settings { .maxRetries = 5,
                                   .initialDelay = std::chrono::milliseconds { 100 },
                                   .backoffMultiplier = 2.0,
                                   .maxDelay = std::chrono::milliseconds { 10'000 } };
    CHECK(detail::CalculateRetryDelay(1, settings) == std::chrono::milliseconds { 200 });
    CHECK(detail::CalculateRetryDelay(2, settings) == std::chrono::milliseconds { 400 });
    CHECK(detail::CalculateRetryDelay(3, settings) == std::chrono::milliseconds { 800 });
}

TEST_CASE("CalculateRetryDelay caps at maxDelay", "[SqlBackup]")
{
    RetrySettings const settings { .maxRetries = 10,
                                   .initialDelay = std::chrono::milliseconds { 1000 },
                                   .backoffMultiplier = 2.0,
                                   .maxDelay = std::chrono::milliseconds { 5000 } };
    // 1000 * 2^4 = 16000ms — must clamp to 5000
    CHECK(detail::CalculateRetryDelay(4, settings) == std::chrono::milliseconds { 5000 });
    CHECK(detail::CalculateRetryDelay(8, settings) == std::chrono::milliseconds { 5000 });
}

TEST_CASE("CalculateRetryDelay handles non-2x multipliers", "[SqlBackup]")
{
    RetrySettings const settings { .maxRetries = 5,
                                   .initialDelay = std::chrono::milliseconds { 100 },
                                   .backoffMultiplier = 1.5,
                                   .maxDelay = std::chrono::milliseconds { 10'000 } };
    // 100 * 1.5 = 150
    CHECK(detail::CalculateRetryDelay(1, settings) == std::chrono::milliseconds { 150 });
    // 100 * 1.5 * 1.5 = 225
    CHECK(detail::CalculateRetryDelay(2, settings) == std::chrono::milliseconds { 225 });
}

// ================================================================================================
// detail::FormatTableName
// ================================================================================================

TEST_CASE("SqlBackup::detail::FormatTableName quotes the table when schema is empty", "[SqlBackup]")
{
    CHECK(detail::FormatTableName("", "Users") == R"("Users")");
}

TEST_CASE("SqlBackup::detail::FormatTableName joins schema and table with quotes", "[SqlBackup]")
{
    CHECK(detail::FormatTableName("dbo", "Users") == R"("dbo"."Users")");
    CHECK(detail::FormatTableName("public", "T_Account") == R"("public"."T_Account")");
}

// ================================================================================================
// detail::CurrentDateTime — ISO 8601 in UTC
// ================================================================================================

TEST_CASE("SqlBackup::detail::CurrentDateTime returns ISO-8601 UTC", "[SqlBackup]")
{
    auto const value = detail::CurrentDateTime();
    // Expect YYYY-MM-DDTHH:MM:SS, possibly with fractional seconds, ending in Z
    std::regex const isoPattern { R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?Z$)" };
    INFO("CurrentDateTime returned: " << value);
    CHECK(std::regex_match(value, isoPattern));
}

// ================================================================================================
// RetrySettings defaults
// ================================================================================================

// ================================================================================================
// CalculateRestoreSettings — auto-tunes batch size, cache size, commit interval, memory limit
// ================================================================================================

TEST_CASE("CalculateRestoreSettings: high-memory / single-worker case", "[SqlBackup]")
{
    constexpr std::size_t mem = std::size_t { 16 } * 1024 * 1024 * 1024; // 16 GB
    auto const settings = Lightweight::SqlBackup::CalculateRestoreSettings(mem, /*concurrency=*/1);

    // batchSize is clamped between 100 and 4000.
    CHECK(settings.batchSize >= 100);
    CHECK(settings.batchSize <= 4000);

    // 16 GB → memoryPerWorker is ample → commit interval 10000.
    CHECK(settings.maxRowsPerCommit == 10000);

    // cacheSizeKB capped at 64 MB worth of KB.
    CHECK(settings.cacheSizeKB <= 65536);

    CHECK(settings.memoryLimitBytes == mem);
}

TEST_CASE("CalculateRestoreSettings: low-memory case lowers commit interval", "[SqlBackup]")
{
    constexpr std::size_t mem = 256 * 1024 * 1024; // 256 MB
    auto const settings = Lightweight::SqlBackup::CalculateRestoreSettings(mem, /*concurrency=*/4);

    // memoryPerWorker is well under 512 MB → 5000 commits.
    CHECK(settings.maxRowsPerCommit == 5000);
    CHECK(settings.batchSize >= 100);
    CHECK(settings.batchSize <= 4000);
    CHECK(settings.memoryLimitBytes == mem);
}

TEST_CASE("CalculateRestoreSettings: zero concurrency is treated as one", "[SqlBackup]")
{
    // The implementation uses std::max(1U, concurrency) — must not divide by zero.
    auto const settings = Lightweight::SqlBackup::CalculateRestoreSettings(1024 * 1024, /*concurrency=*/0);
    CHECK(settings.batchSize >= 100);
    CHECK(settings.batchSize <= 4000);
}

// ================================================================================================
// GetAvailableSystemMemory — must report a positive number on any supported platform
// ================================================================================================

TEST_CASE("GetAvailableSystemMemory returns a positive value", "[SqlBackup]")
{
    auto const mem = Lightweight::SqlBackup::GetAvailableSystemMemory();
    CHECK(mem > 0);
}

// ================================================================================================
// CompressionMethodName covers each enumerator
// ================================================================================================

TEST_CASE("CompressionMethodName produces a non-empty name for every enumerator", "[SqlBackup]")
{
    using Lightweight::SqlBackup::CompressionMethod;
    using Lightweight::SqlBackup::CompressionMethodName;

    CHECK(CompressionMethodName(CompressionMethod::Store) == "store");
    CHECK(CompressionMethodName(CompressionMethod::Deflate) == "deflate");
    CHECK(CompressionMethodName(CompressionMethod::Bzip2) == "bzip2");
    CHECK(CompressionMethodName(CompressionMethod::Lzma) == "lzma");
    CHECK(CompressionMethodName(CompressionMethod::Zstd) == "zstd");
    CHECK(CompressionMethodName(CompressionMethod::Xz) == "xz");
}

TEST_CASE("RetrySettings default-construction matches the documented defaults", "[SqlBackup]")
{
    RetrySettings const settings {};
    CHECK(settings.maxRetries == 3);
    CHECK(settings.initialDelay == std::chrono::milliseconds { 500 });
    CHECK(settings.backoffMultiplier == 2.0);
    CHECK(settings.maxDelay == std::chrono::milliseconds { 30'000 });
}

// ================================================================================================
// detail::ClassifyRetryOutcome
//
// The retry decision extracted out of the retry loops. Pure — no I/O, no handle — so every
// combination of "is it transient" and "is the budget spent" is reachable from a plain unit test.
// ================================================================================================

TEST_CASE("SqlBackup::detail::ClassifyRetryOutcome retries a transient error within budget", "[SqlBackup]")
{
    RetrySettings const settings { .maxRetries = 3 };

    CHECK(detail::ClassifyRetryOutcome(MakeError("08S01"), 0, settings) == detail::RetryAction::Retry);
    CHECK(detail::ClassifyRetryOutcome(MakeError("HYT00"), 1, settings) == detail::RetryAction::Retry);
    CHECK(detail::ClassifyRetryOutcome(MakeError("40001"), 2, settings) == detail::RetryAction::Retry);
}

TEST_CASE("SqlBackup::detail::ClassifyRetryOutcome gives up once the budget is spent", "[SqlBackup]")
{
    RetrySettings const settings { .maxRetries = 3 };

    // attemptsSoFar == maxRetries is the boundary: the budget is spent, so even a transient
    // error must not be retried again.
    CHECK(detail::ClassifyRetryOutcome(MakeError("08S01"), 3, settings) == detail::RetryAction::GiveUp);
    CHECK(detail::ClassifyRetryOutcome(MakeError("08S01"), 4, settings) == detail::RetryAction::GiveUp);
}

TEST_CASE("SqlBackup::detail::ClassifyRetryOutcome gives up on a non-transient error", "[SqlBackup]")
{
    RetrySettings const settings { .maxRetries = 3 };

    // Budget untouched, but the error class is not retryable.
    CHECK(detail::ClassifyRetryOutcome(MakeError("42S02"), 0, settings) == detail::RetryAction::GiveUp);
    CHECK(detail::ClassifyRetryOutcome(MakeError("23000"), 0, settings) == detail::RetryAction::GiveUp);
}

TEST_CASE("SqlBackup::detail::ClassifyRetryOutcome honours a zero retry budget", "[SqlBackup]")
{
    RetrySettings const settings { .maxRetries = 0 };

    CHECK(detail::ClassifyRetryOutcome(MakeError("08S01"), 0, settings) == detail::RetryAction::GiveUp);
}

// ================================================================================================
// detail::RetryOnTransientError
//
// This is the retry policy every backup/restore worker funnels its ODBC calls through, but it had
// no test at all: reaching the retry arm previously meant provoking a real transient driver
// failure mid-backup. Because it is a template over an arbitrary callable, a lambda that throws
// scripted SqlExceptions drives every branch without a database and without touching the
// production sources.
// ================================================================================================

namespace
{

/// Records what the retry loop reports, so a test can assert on the retry messages it emits.
class RecordingProgressManager: public Lightweight::SqlBackup::ProgressManager
{
  public:
    void Update(Lightweight::SqlBackup::Progress const& p) override
    {
        messages.emplace_back(p.message);
        states.emplace_back(p.state);
    }

    void AllDone() override {}

    std::vector<std::string> messages;
    std::vector<Lightweight::SqlBackup::Progress::State> states;
};

/// Retry settings with the backoff collapsed to near-zero so the tests stay fast; the delay
/// arithmetic itself is covered separately by the CalculateRetryDelay cases above.
RetrySettings FastRetry(unsigned maxRetries)
{
    return RetrySettings {
        .maxRetries = maxRetries,
        .initialDelay = std::chrono::milliseconds { 1 },
        .backoffMultiplier = 1.0,
        .maxDelay = std::chrono::milliseconds { 1 },
    };
}

Lightweight::SqlException MakeSqlException(std::string sqlState, std::string message = {})
{
    return Lightweight::SqlException { MakeError(std::move(sqlState), std::move(message)) };
}

} // namespace

TEST_CASE("SqlBackup::detail::RetryOnTransientError returns the value without retrying on success", "[SqlBackup]")
{
    RecordingProgressManager progress;
    auto calls = 0;

    auto const result = detail::RetryOnTransientError(
        [&] {
            ++calls;
            return 42;
        },
        FastRetry(3),
        progress,
        "op");

    CHECK(result == 42);
    CHECK(calls == 1);
    CHECK(progress.messages.empty()); // nothing to report when the first attempt works
}

TEST_CASE("SqlBackup::detail::RetryOnTransientError retries a transient failure and then succeeds", "[SqlBackup]")
{
    RecordingProgressManager progress;
    auto calls = 0;

    auto const result = detail::RetryOnTransientError(
        [&] {
            ++calls;
            if (calls < 3)
                throw MakeSqlException("08S01", "connection dropped");
            return calls;
        },
        FastRetry(5),
        progress,
        "op");

    // Two failures, then the third attempt returns.
    CHECK(result == 3);
    CHECK(calls == 3);
    REQUIRE(progress.messages.size() == 2);
    CHECK(progress.messages[0].contains("retry 1/5"));
    CHECK(progress.messages[1].contains("retry 2/5"));
    CHECK(progress.states[0] == Lightweight::SqlBackup::Progress::State::Warning);
}

TEST_CASE("SqlBackup::detail::RetryOnTransientError rethrows a non-transient error immediately", "[SqlBackup]")
{
    RecordingProgressManager progress;
    auto calls = 0;

    // 42S02 (base table not found) is not in any transient class, so the very first failure
    // propagates without consuming a retry.
    CHECK_THROWS_AS(detail::RetryOnTransientError(
                        [&]() -> int {
                            ++calls;
                            throw MakeSqlException("42S02", "no such table");
                        },
                        FastRetry(3),
                        progress,
                        "op"),
                    Lightweight::SqlException);

    CHECK(calls == 1);
    CHECK(progress.messages.empty());
}

TEST_CASE("SqlBackup::detail::RetryOnTransientError gives up after maxRetries transient failures", "[SqlBackup]")
{
    RecordingProgressManager progress;
    auto calls = 0;

    CHECK_THROWS_AS(detail::RetryOnTransientError(
                        [&]() -> int {
                            ++calls;
                            throw MakeSqlException("HYT00", "timeout");
                        },
                        FastRetry(2),
                        progress,
                        "op"),
                    Lightweight::SqlException);

    // One initial attempt plus maxRetries retries, then the error escapes.
    CHECK(calls == 3);
    CHECK(progress.messages.size() == 2);
}

TEST_CASE("SqlBackup::detail::RetryOnTransientError propagates a void-returning callable", "[SqlBackup]")
{
    RecordingProgressManager progress;
    auto calls = 0;

    // decltype(func()) is void here — a distinct instantiation from the int-returning cases.
    detail::RetryOnTransientError(
        [&] {
            ++calls;
            if (calls == 1)
                throw MakeSqlException("40001", "serialization failure");
        },
        FastRetry(3),
        progress,
        "op");

    CHECK(calls == 2);
    CHECK(progress.messages.size() == 1);
}
