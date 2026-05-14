// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/Common.hpp>
#include <Lightweight/SqlBackup/SqlBackup.hpp>
#include <Lightweight/SqlError.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <regex>
#include <string>

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
