// SPDX-License-Identifier: Apache-2.0

#pragma once

// Optional built-in statistics collection. Part of the Lightweight public API:
// any consumer (tests, tools, examples, downstream apps) inherits the macros.
//
// Configure with `-DLIGHTWEIGHT_ENABLE_STATISTICS=ON`. When the option is OFF,
// this header provides no-op stubs so call sites can use the
// `LIGHTWEIGHT_STATS_*` macros directly without changing the source between
// builds — exactly the pattern TracyProfiler.hpp already establishes.
//
// When Tracy is *also* enabled (`-DLIGHTWEIGHT_ENABLE_TRACY=ON`), every
// recorded sample is additionally emitted as a Tracy plot value, so the same
// instrumentation feeds both the in-process `Snapshot()` API and the Tracy GUI.
//
// @see docs/statistics.md

#include "Api.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <string_view>

namespace Lightweight
{

/// @ingroup CoreApi
/// Identifies the operation class a statistics sample belongs to.
///
/// The enumerators are contiguous and `Count` is the array extent used by
/// @ref SqlStatisticsSnapshot — this is the data-driven table key, not a
/// switch-ladder discriminator.
enum class SqlStatisticsOperation : std::uint8_t
{
    /// `SQLExecute` of a prepared statement.
    Execute,
    /// `SQLExecDirect` of a one-shot statement.
    ExecuteDirect,
    /// `SQLExecute` with a bound parameter array (batch insert/update).
    ExecuteBatch,
    /// `SQLPrepare` of a statement.
    Prepare,
    /// Row / block retrieval (`SQLFetch`, `SQLFetchScroll`).
    ///
    /// @note The library does not currently time its fetch paths, so this slot reads back zero unless
    /// your own code records into it. Row throughput is reported instead by
    /// @ref SqlStatisticsSnapshot::rowsFetched and @ref SqlStatisticsSnapshot::blockFetches.
    Fetch,
    /// Connection acquisition from a @ref Pool.
    PoolAcquire,

    /// Number of enumerators; not an operation itself.
    Count
};

/// @ingroup CoreApi
/// Human-readable name of an operation class, for exporters and log output.
///
/// @param operation The operation to name.
/// @return A stable, dot-free identifier (e.g. `"Execute"`), or `"Unknown"` for an out-of-range value.
[[nodiscard]] LIGHTWEIGHT_API std::string_view ToStringView(SqlStatisticsOperation operation) noexcept;

/// @ingroup CoreApi
/// A fixed-bucket latency histogram, in microseconds.
///
/// Buckets are power-of-two spaced: bucket `i` counts samples in
/// `[2^(i-1), 2^i)` microseconds, with bucket 0 counting `[0, 1)` and the last
/// bucket acting as an open-ended overflow. That covers sub-microsecond calls
/// up to ~35 minutes in @ref BucketCount buckets, and makes bucket selection a
/// single `std::bit_width` rather than a search — cheap enough to sit on the
/// hot path.
///
/// This is the plain-struct snapshot type. It is a value: copy it, export it,
/// diff two of them. The live counters are held separately by @ref SqlStatistics.
struct SqlLatencyHistogram
{
    /// Number of histogram buckets.
    static constexpr std::size_t BucketCount = 32;

    /// Per-bucket sample counts; bucket `i` covers `[2^(i-1), 2^i)` microseconds.
    std::array<std::uint64_t, BucketCount> buckets {};

    /// Total number of samples recorded.
    std::uint64_t count {};

    /// Sum of all sample values, in microseconds.
    std::uint64_t totalMicroseconds {};

    /// Smallest sample recorded, in microseconds; 0 when @ref count is 0.
    std::uint64_t minMicroseconds {};

    /// Largest sample recorded, in microseconds; 0 when @ref count is 0.
    std::uint64_t maxMicroseconds {};

    /// Retrieves the arithmetic mean sample value.
    ///
    /// @return The mean, in microseconds; 0.0 when no samples were recorded.
    [[nodiscard]] LIGHTWEIGHT_API double AverageMicroseconds() const noexcept;

    /// Estimates a percentile from the bucket counts.
    ///
    /// The result is the upper bound of the bucket the percentile falls into,
    /// so it is an over-estimate bounded by a factor of two — the standard
    /// trade-off for a power-of-two histogram, and accurate enough to spot a
    /// regression without storing every sample.
    ///
    /// @param percentile The percentile to estimate, in `[0.0, 1.0]` (e.g. 0.99 for p99).
    /// @return The estimated latency, in microseconds; 0 when no samples were recorded.
    [[nodiscard]] LIGHTWEIGHT_API std::uint64_t PercentileMicroseconds(double percentile) const noexcept;

    /// Retrieves the index of the bucket a given sample value falls into.
    ///
    /// @param microseconds The sample value.
    /// @return The bucket index, clamped to `BucketCount - 1`.
    [[nodiscard]] static constexpr std::size_t BucketOf(std::uint64_t microseconds) noexcept
    {
        // bit_width(0) == 0 -> bucket 0; bit_width(1) == 1 -> bucket 1 ([1,2)); etc.
        auto const width = static_cast<std::size_t>(std::bit_width(microseconds));
        return width < BucketCount ? width : BucketCount - 1;
    }
};

/// @ingroup CoreApi
/// Aggregated counters and latency for one @ref SqlStatisticsOperation class.
struct SqlOperationStatistics
{
    /// Number of operations that completed without raising an ODBC error.
    std::uint64_t succeeded {};

    /// Number of operations that raised an ODBC error.
    std::uint64_t failed {};

    /// Number of operations that were transparently retried (e.g. a stale prepared statement
    /// re-prepared and re-executed). A retried operation is counted once here *and* once in
    /// @ref succeeded or @ref failed according to its final outcome.
    std::uint64_t retried {};

    /// Latency distribution across both successful and failed operations.
    SqlLatencyHistogram latency {};

    /// Retrieves the total number of operations recorded.
    ///
    /// @return `succeeded + failed`.
    [[nodiscard]] constexpr std::uint64_t Total() const noexcept
    {
        return succeeded + failed;
    }
};

/// @ingroup CoreApi
/// Aggregated connection-pool counters.
///
/// Because `Pool` is a class template keyed on a compile-time configuration, a process typically holds
/// several *distinct* pool types. They all record into the process-wide @ref SqlStatistics::Instance —
/// a pool cannot be pointed at a collector of its own — so these counters give a combined view across
/// every pool in the process. The monotonic counters (@ref acquired, @ref reused, @ref waited,
/// @ref released, @ref discarded) aggregate cleanly; @ref idle and @ref checkedOut are last-writer-wins
/// and read as "the most recent pool transition" once more than one pool is in play.
struct SqlPoolStatistics
{
    /// Number of connections handed out (from idle, freshly created, or handed off to a waiter).
    std::uint64_t acquired {};

    /// Of @ref acquired, how many reused an already-open connection rather than creating one.
    std::uint64_t reused {};

    /// Of @ref acquired, how many had to block or park because the pool was exhausted.
    std::uint64_t waited {};

    /// Number of connections returned to the pool.
    std::uint64_t released {};

    /// Number of returned connections destroyed rather than idled, because the pool was over
    /// capacity (`GrowthStrategy::BoundedOverflow`).
    std::uint64_t discarded {};

    /// Connections currently sitting idle in the pool.
    std::uint64_t idle {};

    /// Connections currently checked out of the pool.
    ///
    /// @note Only `GrowthStrategy::BoundedWait` tracks a checked-out count — it is what bounds the
    /// pool. The non-blocking strategies (`UnboundedGrow`, `BoundedOverflow`) never maintain one, so
    /// this reads 0 for them; use @ref acquired minus @ref released there instead.
    std::uint64_t checkedOut {};

    /// Distribution of the time spent waiting for a connection to become available. Only
    /// acquisitions that actually waited contribute a sample.
    SqlLatencyHistogram waitLatency {};

    /// Retrieves the fraction of acquisitions served by an already-open connection.
    ///
    /// @return The reuse rate in `[0.0, 1.0]`; 0.0 when nothing was acquired yet.
    [[nodiscard]] LIGHTWEIGHT_API double ReuseRate() const noexcept;
};

/// @ingroup CoreApi
/// An immutable, plain-struct view of everything a @ref SqlStatistics collector has observed.
///
/// Deliberately free of any exporter-specific concept: read the fields and feed them to Prometheus,
/// StatsD, a log line, or a test assertion. Obtain one via @ref SqlStatistics::Snapshot.
///
/// @note The snapshot is *not* atomic as a whole. Individual counters are read with relaxed
/// ordering while other threads may still be recording, so two related counters can disagree by a
/// few samples at the edges. This is intentional: a consistent snapshot would require locking the
/// hot path. Treat the numbers as monotonically-growing observations, not as a transaction.
struct SqlStatisticsSnapshot
{
    /// Per-operation-class counters, indexed by @ref SqlStatisticsOperation.
    std::array<SqlOperationStatistics, static_cast<std::size_t>(SqlStatisticsOperation::Count)> operations {};

    /// Connection-pool counters.
    SqlPoolStatistics pool {};

    /// Number of connections opened.
    std::uint64_t connectionsOpened {};

    /// Number of connections closed.
    std::uint64_t connectionsClosed {};

    /// Total number of rows fetched across all statements.
    std::uint64_t rowsFetched {};

    /// Number of block-prefetch round-trips (one per `SQLFetchScroll` that materialized a row block).
    std::uint64_t blockFetches {};

    /// Retrieves the counters for one operation class.
    ///
    /// @param operation The operation class to read.
    /// @return A reference to the counters for @p operation.
    [[nodiscard]] constexpr SqlOperationStatistics const& operator[](SqlStatisticsOperation operation) const noexcept
    {
        return operations[static_cast<std::size_t>(operation)];
    }
};

/// @ingroup CoreApi
/// Thread-safe, lock-free aggregator of SQL execution and pool statistics.
///
/// The collector is only *populated* when the library is built with
/// `-DLIGHTWEIGHT_ENABLE_STATISTICS=ON`; otherwise the instrumentation macros compile to nothing
/// and every snapshot reads back zero. The type itself always exists, so downstream code that reads
/// statistics still compiles in a build that does not collect them.
///
/// All recording methods use relaxed atomics: they never block, never allocate, and are safe to
/// call from any thread. See @ref SqlStatisticsSnapshot for the consistency caveat that buys.
///
/// @code
/// auto const stats = Lightweight::SqlStatistics::Instance().Snapshot();
/// std::println("executes: {}, p99: {}us",
///              stats[Lightweight::SqlStatisticsOperation::Execute].Total(),
///              stats[Lightweight::SqlStatisticsOperation::Execute].latency.PercentileMicroseconds(0.99));
/// @endcode
///
/// @see SqlStatisticsSnapshot, docs/statistics.md
class SqlStatistics
{
  public:
    /// Constructs a collector with every counter at zero.
    SqlStatistics() noexcept = default;
    ~SqlStatistics() = default;

    SqlStatistics(SqlStatistics const&) = delete;
    SqlStatistics& operator=(SqlStatistics const&) = delete;
    SqlStatistics(SqlStatistics&&) = delete;
    SqlStatistics& operator=(SqlStatistics&&) = delete;

    /// Retrieves the process-wide collector that the library's own instrumentation records into.
    ///
    /// @return A reference to the singleton collector.
    [[nodiscard]] LIGHTWEIGHT_API static SqlStatistics& Instance() noexcept;

    /// Records one completed operation.
    ///
    /// @param operation The operation class.
    /// @param duration How long the operation took.
    /// @param failed Whether the operation raised an ODBC error.
    LIGHTWEIGHT_API void RecordOperation(SqlStatisticsOperation operation,
                                         std::chrono::microseconds duration,
                                         bool failed) noexcept;

    /// Records that an operation was transparently retried.
    ///
    /// @param operation The operation class being retried.
    LIGHTWEIGHT_API void RecordRetry(SqlStatisticsOperation operation) noexcept;

    /// Records rows produced by a fetch.
    ///
    /// @param rowCount Number of rows materialized.
    /// @param wasBlockFetch Whether the rows came from a single block-prefetch round-trip.
    LIGHTWEIGHT_API void RecordRowsFetched(std::uint64_t rowCount, bool wasBlockFetch) noexcept;

    /// Records a connection being opened.
    LIGHTWEIGHT_API void RecordConnectionOpened() noexcept;

    /// Records a connection being closed.
    LIGHTWEIGHT_API void RecordConnectionClosed() noexcept;

    /// Records a connection acquisition from a pool.
    ///
    /// @param waitDuration Time spent waiting for the connection; zero when it was available immediately.
    /// @param reused Whether an already-open connection was handed out rather than a fresh one created.
    /// @param waited Whether the caller actually had to block or park.
    LIGHTWEIGHT_API void RecordPoolAcquire(std::chrono::microseconds waitDuration, bool reused, bool waited) noexcept;

    /// Records a connection being returned to a pool.
    ///
    /// @param discarded Whether the connection was destroyed instead of idled (pool over capacity).
    LIGHTWEIGHT_API void RecordPoolRelease(bool discarded) noexcept;

    /// Records the pool's current occupancy. Called whenever the pool's composition changes.
    ///
    /// @param idle Connections currently idle.
    /// @param checkedOut Connections currently checked out.
    LIGHTWEIGHT_API void RecordPoolOccupancy(std::uint64_t idle, std::uint64_t checkedOut) noexcept;

    /// Retrieves a point-in-time copy of every counter.
    ///
    /// @return The snapshot; all-zero in a build without `LIGHTWEIGHT_ENABLE_STATISTICS`.
    [[nodiscard]] LIGHTWEIGHT_API SqlStatisticsSnapshot Snapshot() const noexcept;

    /// Resets every counter back to zero. Intended for tests and for exporters that report deltas.
    LIGHTWEIGHT_API void Reset() noexcept;

    /// Indicates whether this build actually collects statistics.
    ///
    /// @return `true` when built with `-DLIGHTWEIGHT_ENABLE_STATISTICS=ON`, `false` otherwise.
    [[nodiscard]] static constexpr bool IsEnabled() noexcept
    {
#if defined(LIGHTWEIGHT_STATISTICS_ENABLED)
        return true;
#else
        return false;
#endif
    }

  private:
    /// Lock-free mirror of @ref SqlLatencyHistogram holding the live counters.
    struct AtomicHistogram
    {
        std::array<std::atomic<std::uint64_t>, SqlLatencyHistogram::BucketCount> buckets {};
        std::atomic<std::uint64_t> count {};
        std::atomic<std::uint64_t> totalMicroseconds {};
        std::atomic<std::uint64_t> minMicroseconds { (std::numeric_limits<std::uint64_t>::max)() };
        std::atomic<std::uint64_t> maxMicroseconds {};

        void Record(std::uint64_t microseconds) noexcept;
        [[nodiscard]] SqlLatencyHistogram Load() const noexcept;
        void Reset() noexcept;
    };

    /// Lock-free mirror of @ref SqlOperationStatistics.
    struct AtomicOperation
    {
        std::atomic<std::uint64_t> succeeded {};
        std::atomic<std::uint64_t> failed {};
        std::atomic<std::uint64_t> retried {};
        AtomicHistogram latency {};
    };

    std::array<AtomicOperation, static_cast<std::size_t>(SqlStatisticsOperation::Count)> _operations {};

    std::atomic<std::uint64_t> _poolAcquired {};
    std::atomic<std::uint64_t> _poolReused {};
    std::atomic<std::uint64_t> _poolWaited {};
    std::atomic<std::uint64_t> _poolReleased {};
    std::atomic<std::uint64_t> _poolDiscarded {};
    std::atomic<std::uint64_t> _poolIdle {};
    std::atomic<std::uint64_t> _poolCheckedOut {};
    AtomicHistogram _poolWaitLatency {};

    std::atomic<std::uint64_t> _connectionsOpened {};
    std::atomic<std::uint64_t> _connectionsClosed {};
    std::atomic<std::uint64_t> _rowsFetched {};
    std::atomic<std::uint64_t> _blockFetches {};
};

/// @ingroup CoreApi
/// RAII scope that times its enclosing region and records it as one operation.
///
/// Construct at the top of the region; the destructor records the elapsed time. Call @ref Failed to
/// mark the operation as errored (the destructor still records the latency, so a failing statement
/// contributes to the distribution rather than silently vanishing from it).
///
/// Prefer the @c LIGHTWEIGHT_STATS_SCOPE macro, which compiles the object away entirely when
/// statistics are disabled.
class SqlStatisticsScope
{
  public:
    /// Starts timing a region.
    ///
    /// @param operation The operation class to record under.
    explicit SqlStatisticsScope(SqlStatisticsOperation operation) noexcept:
        _operation { operation },
        _startedAt { std::chrono::steady_clock::now() },
        _uncaughtOnEntry { std::uncaught_exceptions() }
    {
    }

    SqlStatisticsScope(SqlStatisticsScope const&) = delete;
    SqlStatisticsScope& operator=(SqlStatisticsScope const&) = delete;
    SqlStatisticsScope(SqlStatisticsScope&&) = delete;
    SqlStatisticsScope& operator=(SqlStatisticsScope&&) = delete;

    ~SqlStatisticsScope()
    {
        auto const elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - _startedAt);
        // Leaving via an exception *is* the failure signal on this code base: RequireSuccess throws
        // SqlException on a non-successful ODBC return. Comparing the in-flight exception count
        // rather than a flag means every throwing path is classified without touching its call site.
        auto const failed = _failed || std::uncaught_exceptions() > _uncaughtOnEntry;
        SqlStatistics::Instance().RecordOperation(_operation, elapsed, failed);
    }

    /// Marks the timed operation as having failed.
    void Failed() noexcept
    {
        _failed = true;
    }

    /// Records that the timed operation was transparently retried.
    void Retried() noexcept
    {
        SqlStatistics::Instance().RecordRetry(_operation);
    }

  private:
    SqlStatisticsOperation _operation;
    std::chrono::steady_clock::time_point _startedAt;
    int _uncaughtOnEntry;
    bool _failed = false;
};

} // namespace Lightweight

// {{{ Instrumentation macros
//
// Mirrors the TracyProfiler.hpp convention: when the feature is off, every macro consumes ALL of
// its arguments via `(void) (...)` so call sites never trip unused-variable warnings.

#if defined(LIGHTWEIGHT_STATISTICS_ENABLED)

    /// Times the enclosing scope and records it under @p op, naming the scope object @p var.
    #define LIGHTWEIGHT_STATS_SCOPE_V(var, op) \
        ::Lightweight::SqlStatisticsScope var  \
        {                                      \
            op                                 \
        }

    /// Times the enclosing scope and records it under @p op.
    #define LIGHTWEIGHT_STATS_SCOPE(op) LIGHTWEIGHT_STATS_SCOPE_V(lightweightStatsScope_, op)

    /// Marks the scope named @p var as failed.
    #define LIGHTWEIGHT_STATS_FAILED(var) (var).Failed()

    /// Marks the scope named @p var as retried.
    #define LIGHTWEIGHT_STATS_RETRIED(var) (var).Retried()

    /// Records @p rows fetched; @p isBlock tells whether they came from one block round-trip.
    #define LIGHTWEIGHT_STATS_ROWS(rows, isBlock) \
        ::Lightweight::SqlStatistics::Instance().RecordRowsFetched((rows), (isBlock))

    /// Records a connection being opened.
    #define LIGHTWEIGHT_STATS_CONNECTION_OPENED() ::Lightweight::SqlStatistics::Instance().RecordConnectionOpened()

    /// Records a connection being closed.
    #define LIGHTWEIGHT_STATS_CONNECTION_CLOSED() ::Lightweight::SqlStatistics::Instance().RecordConnectionClosed()

    /// Records a pool acquisition: @p wait duration, whether it @p reused, whether it @p waited.
    #define LIGHTWEIGHT_STATS_POOL_ACQUIRE(wait, reused, waited) \
        ::Lightweight::SqlStatistics::Instance().RecordPoolAcquire((wait), (reused), (waited))

    /// Records a pool release; @p discarded tells whether the connection was destroyed.
    #define LIGHTWEIGHT_STATS_POOL_RELEASE(discarded) ::Lightweight::SqlStatistics::Instance().RecordPoolRelease((discarded))

    /// Records current pool occupancy.
    #define LIGHTWEIGHT_STATS_POOL_OCCUPANCY(idle, checkedOut) \
        ::Lightweight::SqlStatistics::Instance().RecordPoolOccupancy((idle), (checkedOut))

#else

    #define LIGHTWEIGHT_STATS_SCOPE_V(var, op)    ((void) sizeof(op))
    #define LIGHTWEIGHT_STATS_SCOPE(op)           ((void) sizeof(op))
    #define LIGHTWEIGHT_STATS_FAILED(var)         ((void) 0)
    #define LIGHTWEIGHT_STATS_RETRIED(var)        ((void) 0)
    #define LIGHTWEIGHT_STATS_ROWS(rows, isBlock) (((void) sizeof(rows)), ((void) sizeof(isBlock)))
    #define LIGHTWEIGHT_STATS_CONNECTION_OPENED() ((void) 0)
    #define LIGHTWEIGHT_STATS_CONNECTION_CLOSED() ((void) 0)
    #define LIGHTWEIGHT_STATS_POOL_ACQUIRE(wait, reused, waited) \
        (((void) sizeof(wait)), ((void) sizeof(reused)), ((void) sizeof(waited)))
    #define LIGHTWEIGHT_STATS_POOL_RELEASE(discarded)          ((void) sizeof(discarded))
    #define LIGHTWEIGHT_STATS_POOL_OCCUPANCY(idle, checkedOut) (((void) sizeof(idle)), ((void) sizeof(checkedOut)))

#endif
// }}}
