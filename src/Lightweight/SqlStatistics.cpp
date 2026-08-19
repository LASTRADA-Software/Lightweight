// SPDX-License-Identifier: Apache-2.0

#include "SqlStatistics.hpp"
#include "TracyProfiler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Lightweight
{

namespace
{
    // Recording is compiled out wholesale when statistics are disabled, so the public API keeps its
    // shape (downstream code that reads a snapshot still compiles) while the hot path pays nothing.
    constexpr bool CollectingEnabled = SqlStatistics::IsEnabled();

    /// Tracy plot names, indexed by SqlStatisticsOperation. Data-driven rather than a switch ladder,
    /// so adding an operation is a single table row.
    constexpr std::array<std::string_view, static_cast<std::size_t>(SqlStatisticsOperation::Count)> OperationNames {
        "Execute", "ExecuteDirect", "ExecuteBatch", "Prepare", "Fetch", "PoolAcquire"
    };

#if defined(LIGHTWEIGHT_STATISTICS_ENABLED) && defined(LIGHTWEIGHT_TRACY_ENABLED)
    /// Tracy plot labels must outlive the call, so they are static string literals rather than
    /// formatted at the call site.
    constexpr std::array<char const*, static_cast<std::size_t>(SqlStatisticsOperation::Count)> LatencyPlotNames {
        "Sql.Execute.us", "Sql.ExecuteDirect.us", "Sql.ExecuteBatch.us",
        "Sql.Prepare.us", "Sql.Fetch.us",         "Sql.PoolAcquire.us"
    };
#endif

    /// Emits a sample to Tracy when Tracy is enabled; a no-op otherwise.
    ///
    /// Marked `[[maybe_unused]]` because its only call site sits inside `if constexpr
    /// (CollectingEnabled)`: with statistics disabled that branch is never emitted, leaving this
    /// internal-linkage function referenced but never generated — which clang rejects under
    /// `-Wunneeded-internal-declaration -Werror` (i.e. the project's own default build).
    ///
    /// @param operation The operation the sample belongs to.
    /// @param microseconds The sampled duration.
    [[maybe_unused]] void PlotLatency([[maybe_unused]] SqlStatisticsOperation operation,
                                      [[maybe_unused]] std::uint64_t microseconds) noexcept
    {
#if defined(LIGHTWEIGHT_STATISTICS_ENABLED) && defined(LIGHTWEIGHT_TRACY_ENABLED)
        auto const index = static_cast<std::size_t>(operation);
        if (index < LatencyPlotNames.size())
            TracyPlot(LatencyPlotNames[index], static_cast<int64_t>(microseconds));
#endif
    }

} // namespace

std::string_view ToStringView(SqlStatisticsOperation operation) noexcept
{
    auto const index = static_cast<std::size_t>(operation);
    if (index < OperationNames.size())
        return OperationNames[index];
    return "Unknown";
}

double SqlLatencyHistogram::AverageMicroseconds() const noexcept
{
    if (count == 0)
        return 0.0;
    return static_cast<double>(totalMicroseconds) / static_cast<double>(count);
}

std::uint64_t SqlLatencyHistogram::PercentileMicroseconds(double percentile) const noexcept
{
    if (count == 0)
        return 0;

    // std::clamp passes NaN straight through (both of its comparisons are false for NaN), and the
    // float-to-integer cast below would then be undefined behaviour — so reject non-finite input first.
    if (std::isnan(percentile))
        percentile = 0.0;
    percentile = std::clamp(percentile, 0.0, 1.0);

    // Rank of the sample we are looking for, 1-based: p=1.0 selects the last sample.
    auto const rank =
        (std::max) (std::uint64_t { 1 }, static_cast<std::uint64_t>(std::ceil(percentile * static_cast<double>(count))));

    auto cumulative = std::uint64_t { 0 };
    for (auto bucket = std::size_t { 0 }; bucket != BucketCount; ++bucket)
    {
        cumulative += buckets[bucket];
        if (cumulative >= rank)
        {
            // Bucket i spans [2^(i-1), 2^i), so its largest representable value is 2^i - 1
            // (bucket 0 holds only 0). Reporting the bucket's upper bound is what makes this an
            // over-estimate bounded by a factor of two.
            auto const upperBound = bucket == 0 ? std::uint64_t { 0 } : ((std::uint64_t { 1 } << bucket) - 1);
            // Never report beyond the largest sample actually seen — that keeps p100 exact and stops
            // the power-of-two rounding from inventing latency the caller never observed.
            return (std::min) (upperBound, maxMicroseconds);
        }
    }
    return maxMicroseconds;
}

double SqlPoolStatistics::ReuseRate() const noexcept
{
    if (acquired == 0)
        return 0.0;
    return static_cast<double>(reused) / static_cast<double>(acquired);
}

// {{{ SqlStatistics::AtomicHistogram

void SqlStatistics::AtomicHistogram::Record(std::uint64_t microseconds) noexcept
{
    buckets[SqlLatencyHistogram::BucketOf(microseconds)].fetch_add(1, std::memory_order_relaxed);
    count.fetch_add(1, std::memory_order_relaxed);
    totalMicroseconds.fetch_add(microseconds, std::memory_order_relaxed);

    // CAS loops rather than plain stores: two threads racing on the extremes must not lose an update.
    auto currentMin = minMicroseconds.load(std::memory_order_relaxed);
    while (microseconds < currentMin
           && !minMicroseconds.compare_exchange_weak(
               currentMin, microseconds, std::memory_order_relaxed, std::memory_order_relaxed))
        ;

    auto currentMax = maxMicroseconds.load(std::memory_order_relaxed);
    while (microseconds > currentMax
           && !maxMicroseconds.compare_exchange_weak(
               currentMax, microseconds, std::memory_order_relaxed, std::memory_order_relaxed))
        ;
}

SqlLatencyHistogram SqlStatistics::AtomicHistogram::Load() const noexcept
{
    auto result = SqlLatencyHistogram {};
    for (auto bucket = std::size_t { 0 }; bucket != SqlLatencyHistogram::BucketCount; ++bucket)
        result.buckets[bucket] = buckets[bucket].load(std::memory_order_relaxed);
    result.count = count.load(std::memory_order_relaxed);
    result.totalMicroseconds = totalMicroseconds.load(std::memory_order_relaxed);
    auto const loadedMin = minMicroseconds.load(std::memory_order_relaxed);
    // The sentinel means "nothing recorded yet" — report 0 rather than UINT64_MAX.
    result.minMicroseconds = loadedMin == (std::numeric_limits<std::uint64_t>::max)() ? 0 : loadedMin;
    result.maxMicroseconds = maxMicroseconds.load(std::memory_order_relaxed);
    return result;
}

void SqlStatistics::AtomicHistogram::Reset() noexcept
{
    for (auto& bucket: buckets)
        bucket.store(0, std::memory_order_relaxed);
    count.store(0, std::memory_order_relaxed);
    totalMicroseconds.store(0, std::memory_order_relaxed);
    minMicroseconds.store((std::numeric_limits<std::uint64_t>::max)(), std::memory_order_relaxed);
    maxMicroseconds.store(0, std::memory_order_relaxed);
}

// }}}

SqlStatistics& SqlStatistics::Instance() noexcept
{
    static SqlStatistics theInstance;
    return theInstance;
}

void SqlStatistics::RecordOperation(SqlStatisticsOperation operation,
                                    std::chrono::microseconds duration,
                                    bool failed) noexcept
{
    if constexpr (CollectingEnabled)
    {
        auto const index = static_cast<std::size_t>(operation);
        if (index < _operations.size())
        {
            auto& slot = _operations[index];
            (failed ? slot.failed : slot.succeeded).fetch_add(1, std::memory_order_relaxed);

            auto const microseconds = static_cast<std::uint64_t>((std::max) (duration.count(), std::int64_t { 0 }));
            slot.latency.Record(microseconds);
            PlotLatency(operation, microseconds);
        }
    }
}

void SqlStatistics::RecordRetry(SqlStatisticsOperation operation) noexcept
{
    if constexpr (CollectingEnabled)
    {
        auto const index = static_cast<std::size_t>(operation);
        if (index < _operations.size())
            _operations[index].retried.fetch_add(1, std::memory_order_relaxed);
    }
}

void SqlStatistics::RecordRowsFetched(std::uint64_t rowCount, bool wasBlockFetch) noexcept
{
    if constexpr (CollectingEnabled)
    {
        _rowsFetched.fetch_add(rowCount, std::memory_order_relaxed);
        if (wasBlockFetch)
            _blockFetches.fetch_add(1, std::memory_order_relaxed);
    }
}

void SqlStatistics::RecordConnectionOpened() noexcept
{
    if constexpr (CollectingEnabled)
        _connectionsOpened.fetch_add(1, std::memory_order_relaxed);
}

void SqlStatistics::RecordConnectionClosed() noexcept
{
    if constexpr (CollectingEnabled)
        _connectionsClosed.fetch_add(1, std::memory_order_relaxed);
}

void SqlStatistics::RecordPoolAcquire(std::chrono::microseconds waitDuration, bool reused, bool waited) noexcept
{
    if constexpr (CollectingEnabled)
    {
        _poolAcquired.fetch_add(1, std::memory_order_relaxed);
        if (reused)
            _poolReused.fetch_add(1, std::memory_order_relaxed);

        if (waited)
        {
            _poolWaited.fetch_add(1, std::memory_order_relaxed);
            // Only genuine waits contribute a sample; otherwise the distribution is drowned in zeros.
            _poolWaitLatency.Record(static_cast<std::uint64_t>((std::max) (waitDuration.count(), std::int64_t { 0 })));
        }

        // The PoolAcquire operation slot tracks every acquisition, waited or not.
        RecordOperation(SqlStatisticsOperation::PoolAcquire, waitDuration, false);
    }
}

void SqlStatistics::RecordPoolRelease(bool discarded) noexcept
{
    if constexpr (CollectingEnabled)
    {
        _poolReleased.fetch_add(1, std::memory_order_relaxed);
        if (discarded)
            _poolDiscarded.fetch_add(1, std::memory_order_relaxed);
    }
}

void SqlStatistics::RecordPoolOccupancy(std::uint64_t idle, std::uint64_t checkedOut) noexcept
{
    if constexpr (CollectingEnabled)
    {
        _poolIdle.store(idle, std::memory_order_relaxed);
        _poolCheckedOut.store(checkedOut, std::memory_order_relaxed);
    }

#if defined(LIGHTWEIGHT_STATISTICS_ENABLED) && defined(LIGHTWEIGHT_TRACY_ENABLED)
    TracyPlot("Sql.Pool.Idle", static_cast<int64_t>(idle));
    TracyPlot("Sql.Pool.CheckedOut", static_cast<int64_t>(checkedOut));
#endif
}

SqlStatisticsSnapshot SqlStatistics::Snapshot() const noexcept
{
    auto result = SqlStatisticsSnapshot {};

    for (auto index = std::size_t { 0 }; index != _operations.size(); ++index)
    {
        auto const& slot = _operations[index];
        auto& out = result.operations[index];
        out.succeeded = slot.succeeded.load(std::memory_order_relaxed);
        out.failed = slot.failed.load(std::memory_order_relaxed);
        out.retried = slot.retried.load(std::memory_order_relaxed);
        out.latency = slot.latency.Load();
    }

    result.pool.acquired = _poolAcquired.load(std::memory_order_relaxed);
    result.pool.reused = _poolReused.load(std::memory_order_relaxed);
    result.pool.waited = _poolWaited.load(std::memory_order_relaxed);
    result.pool.released = _poolReleased.load(std::memory_order_relaxed);
    result.pool.discarded = _poolDiscarded.load(std::memory_order_relaxed);
    result.pool.idle = _poolIdle.load(std::memory_order_relaxed);
    result.pool.checkedOut = _poolCheckedOut.load(std::memory_order_relaxed);
    result.pool.waitLatency = _poolWaitLatency.Load();

    result.connectionsOpened = _connectionsOpened.load(std::memory_order_relaxed);
    result.connectionsClosed = _connectionsClosed.load(std::memory_order_relaxed);
    result.rowsFetched = _rowsFetched.load(std::memory_order_relaxed);
    result.blockFetches = _blockFetches.load(std::memory_order_relaxed);

    return result;
}

void SqlStatistics::Reset() noexcept
{
    for (auto& slot: _operations)
    {
        slot.succeeded.store(0, std::memory_order_relaxed);
        slot.failed.store(0, std::memory_order_relaxed);
        slot.retried.store(0, std::memory_order_relaxed);
        slot.latency.Reset();
    }

    _poolAcquired.store(0, std::memory_order_relaxed);
    _poolReused.store(0, std::memory_order_relaxed);
    _poolWaited.store(0, std::memory_order_relaxed);
    _poolReleased.store(0, std::memory_order_relaxed);
    _poolDiscarded.store(0, std::memory_order_relaxed);
    _poolIdle.store(0, std::memory_order_relaxed);
    _poolCheckedOut.store(0, std::memory_order_relaxed);
    _poolWaitLatency.Reset();

    _connectionsOpened.store(0, std::memory_order_relaxed);
    _connectionsClosed.store(0, std::memory_order_relaxed);
    _rowsFetched.store(0, std::memory_order_relaxed);
    _blockFetches.store(0, std::memory_order_relaxed);
}

} // namespace Lightweight
