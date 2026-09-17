// SPDX-License-Identifier: Apache-2.0
//
// Runtime benchmark for the prepared-statement cache (issue #552).
//
// Measures wall-clock time of workloads that re-prepare the same SQL text, with the connection's
// prepared-statement cache disabled (the default) and enabled, so the feature can be judged on
// numbers rather than on the expectation that skipping SQLPrepare must be faster.
//
//     LightweightPreparedStatementCacheBenchmark [iterations] [connectionString] [repetitions] [sections] [seedRows]
//
// `sections` is `single` (one connection), `pool` (the pooled workloads) or `all` (the default). The
// split matters when measuring against a high-latency link, where the pooled shapes spend minutes
// opening connections.
//
// The reported prepare counts come from a SqlLogger subclass and from the cache's own hit/miss
// counters, so a scenario that accidentally stops exercising the cache shows up as a count rather
// than as a suspiciously small delta.

#include <Lightweight/Lightweight.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace Lightweight;
using namespace std::chrono;

// Member pointers are spelled differently in the C++26 reflection build; mirrors relations_runtime.cpp.
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    #define Member(x) ^^x
#else
    #define Member(x) &x
#endif

// Deliberately at namespace scope: the reflection layer takes the address of an `extern const T`,
// which a type with internal linkage cannot provide.
struct Item
{
    static constexpr std::string_view TableName = "BenchCacheItem";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<40>> name {};
    Field<int32_t> value {};
};

namespace
{

/// Counts the logical Prepare() calls, i.e. the ones the library sees - whether or not they reach
/// the driver. Combined with the cache's miss counter this separates "asked to prepare" from
/// "actually issued SQLPrepare".
class CountingLogger final: public SqlLogger::Null
{
  public:
    // Atomic because the pooled workloads below drive several worker threads through one logger.
    std::atomic<size_t> prepares { 0 };

    void OnPrepare(std::string_view const& /*query*/) override
    {
        prepares.fetch_add(1, std::memory_order_relaxed);
    }

    void Reset() noexcept
    {
        prepares.store(0, std::memory_order_relaxed);
    }
};

CountingLogger g_logger;

size_t g_repetitions = 5;

/// One measurement of a scenario under one cache setting.
struct Measurement
{
    double milliseconds {};
    size_t logicalPrepares {};
    uint64_t cacheHits {};
    uint64_t cacheMisses {};
};

/// Times one run of @p body with the cache set to @p capacity, folding the result into @p out.
template <typename Body>
void RunOnce(SqlConnection& connection, size_t capacity, Measurement& out, Body&& body)
{
    connection.SetPreparedStatementCacheCapacity(capacity);
    g_logger.Reset();
    connection.PreparedStatementCache().ResetStatistics();

    auto const start = steady_clock::now();
    body();
    auto const elapsed = duration<double, std::milli>(steady_clock::now() - start).count();

    out.milliseconds = std::min(out.milliseconds, elapsed);
    out.logicalPrepares = g_logger.prepares;
    out.cacheHits = connection.PreparedStatementCache().Stats().hits;
    out.cacheMisses = connection.PreparedStatementCache().Stats().misses;

    connection.SetPreparedStatementCacheCapacity(0);
    g_logger.Reset();
}

/// Measures @p body with the cache off and on, alternating the two settings inside one loop rather
/// than running every "off" repetition first. A shared database host drifts over the minutes such a
/// comparison takes, and interleaving charges that drift to both settings instead of to whichever one
/// happened to run while the host was busy. The fastest run of each setting is reported, so a cold
/// page cache or a one-off server-side plan compilation is charged to neither. The counters describe
/// the last repetition alone.
template <typename Body>
std::pair<Measurement, Measurement> MeasureBoth(SqlConnection& connection, Body&& body)
{
    auto off = Measurement { .milliseconds = 1e18 };
    auto on = Measurement { .milliseconds = 1e18 };
    for (size_t i = 0; i < g_repetitions; ++i)
    {
        RunOnce(connection, 0, off, body);
        RunOnce(connection, PreparedStatementCacheCapacitySuggested, on, body);
    }
    return { off, on };
}

void Report(char const* name, std::pair<Measurement, Measurement> const& measurements)
{
    auto const& [off, on] = measurements;
    // A speed-up below 1.0 means the cache made this scenario slower - reported as such rather than
    // clamped, since the point of the exercise is to find out whether that happens.
    auto const speedup = on.milliseconds > 0.0 ? off.milliseconds / on.milliseconds : 0.0;
    std::printf("%-44s %9.2f %9.2f   %6.2fx   prepare=%-6zu SQLPrepare=%-6llu hit=%llu\n",
                name,
                off.milliseconds,
                on.milliseconds,
                speedup,
                on.logicalPrepares,
                static_cast<unsigned long long>(on.cacheMisses),
                static_cast<unsigned long long>(on.cacheHits));
}

/// Scenario 1: the shape every layer above SqlStatement produces - a fresh statement per call site,
/// preparing SQL text the connection has already seen. The same-statement fast path cannot help
/// here, so this is the cache's core case.
size_t FreshStatementPerIteration(SqlConnection& connection, std::string_view sql, size_t iterations)
{
    size_t rows = 0;
    for (size_t i = 1; i <= iterations; ++i)
    {
        auto stmt = SqlStatement { connection };
        stmt.Prepare(sql);
        auto cursor = stmt.Execute(static_cast<uint64_t>(i));
        while (cursor.FetchRow())
            ++rows;
    }
    return rows;
}

/// Scenario 2: preparing only, without executing - isolates the SQLPrepare round-trip from the
/// execute/fetch work that surrounds it in every realistic workload.
void FreshStatementPrepareOnly(SqlConnection& connection, std::string_view sql, size_t iterations)
{
    for (size_t i = 0; i < iterations; ++i)
    {
        auto stmt = SqlStatement { connection };
        stmt.Prepare(sql);
    }
}

/// Scenario 3: one long-lived statement alternating between several query texts. Without the cache
/// every switch re-prepares, because SqlStatement's own fast path only covers a repeat of the text
/// it currently holds.
size_t InterleavedQueries(SqlConnection& connection, std::span<std::string const> queries, size_t iterations)
{
    size_t rows = 0;
    auto stmt = SqlStatement { connection };
    for (size_t i = 0; i < iterations; ++i)
    {
        stmt.Prepare(queries[i % queries.size()]);
        auto cursor = stmt.Execute(static_cast<uint64_t>(i % 100) + 1);
        while (cursor.FetchRow())
            ++rows;
    }
    return rows;
}

// --- Pooled workloads -------------------------------------------------------------------------
//
// A prepared handle is a child of one connection's SQLHDBC, so the cache can never be shared between
// pooled connections: every connection a pool hands out warms up on its own. These scenarios measure
// what that costs and what survives it.

/// The prepared-statement cache counters summed over every pooled connection a run touched. Each
/// worker snapshots its connection's counters at acquire and adds the delta back at release, which is
/// the only way to total them: the pool owns the connections and does not expose them.
struct PoolCacheCounters
{
    std::atomic<uint64_t> hits { 0 };
    std::atomic<uint64_t> misses { 0 };
    std::atomic<uint64_t> directReuses { 0 };

    void Add(SqlPreparedStatementCache::Statistics const& before,
             SqlPreparedStatementCache::Statistics const& after) noexcept
    {
        hits.fetch_add(after.hits - before.hits, std::memory_order_relaxed);
        misses.fetch_add(after.misses - before.misses, std::memory_order_relaxed);
        directReuses.fetch_add(after.directReuses - before.directReuses, std::memory_order_relaxed);
    }

    void Reset() noexcept
    {
        hits.store(0, std::memory_order_relaxed);
        misses.store(0, std::memory_order_relaxed);
        directReuses.store(0, std::memory_order_relaxed);
    }
};

/// One unit of work: acquire a mapper, run a mix of query shapes through it, hand it back. Three
/// distinct query texts, because a pool whose connections each cache one statement says nothing about
/// how a real working set behaves.
template <typename PooledMapper>
size_t QueryMix(PooledMapper& pooled, size_t index, size_t seedRows)
{
    auto& dm = pooled.Get();
    size_t rows = 0;

    // (1) By primary key - goes through the mapper's own long-lived statement.
    if (dm.template QuerySingle<Item, DataMapperOptions { .loadRelations = false }>(static_cast<uint64_t>(index % seedRows)
                                                                                    + 1))
        ++rows;

    // (2) and (3) Query-builder reads - a fresh SqlStatement per call, the shape the cache targets.
    rows += dm.template Query<Item, DataMapperOptions { .loadRelations = false }>()
                .Where(FieldNameOf<Member(Item::value)>, static_cast<int32_t>(index % seedRows))
                .All()
                .size();
    rows += static_cast<size_t>(dm.template Query<Item, DataMapperOptions { .loadRelations = false }>()
                                    .Where(FieldNameOf<Member(Item::id)>, "<=", static_cast<uint64_t>(seedRows))
                                    .Count());
    return rows;
}

/// Runs @p operations units of work spread over @p workers threads against @p pool.
template <typename PoolType>
void RunPoolWorkload(PoolType& pool, size_t workers, size_t operations, size_t seedRows, PoolCacheCounters& counters)
{
    auto const worker = [&](size_t workerIndex) {
        for (size_t i = workerIndex; i < operations; i += workers)
        {
            auto pooled = pool.Acquire();
            auto const before = pooled.Get().Connection().PreparedStatementCache().Stats();
            std::ignore = QueryMix(pooled, i, seedRows);
            counters.Add(before, pooled.Get().Connection().PreparedStatementCache().Stats());
        }
    };

    auto threads = std::vector<std::jthread> {};
    threads.reserve(workers);
    for (size_t w = 0; w < workers; ++w)
        threads.emplace_back(worker, w);
}

/// A pooled measurement keeps the cold run apart from the warm ones. The cold run is the interesting
/// one here: it carries the per-connection warm-up the pool cannot amortise away.
struct PoolMeasurement
{
    double coldMilliseconds {};
    double warmMilliseconds { 1e18 };
    /// SQLPrepare calls on the cold run: one per connection per distinct query text, the warm-up a
    /// pool cannot share between its connections.
    uint64_t coldMisses {};
    uint64_t coldHits {};
    /// Same counters on the last (warm) repetition. Non-zero misses here mean connections are being
    /// destroyed and rebuilt faster than their caches can pay for themselves.
    uint64_t warmMisses {};
    uint64_t warmHits {};
    uint64_t warmDirectReuses {};
};

/// Measures one pool shape. The pool is constructed once and reused across repetitions, so connecting
/// is paid once and the first repetition reports the cost of warming every connection's cache.
template <PoolConfig Config>
PoolMeasurement MeasurePool(size_t workers, size_t operations, size_t seedRows)
{
    auto pool = Pool<Config> {};
    auto counters = PoolCacheCounters {};
    auto result = PoolMeasurement {};

    for (size_t i = 0; i < g_repetitions; ++i)
    {
        counters.Reset();
        auto const start = steady_clock::now();
        RunPoolWorkload(pool, workers, operations, seedRows, counters);
        auto const elapsed = duration<double, std::milli>(steady_clock::now() - start).count();

        if (i == 0)
        {
            result.coldMilliseconds = elapsed;
            result.coldMisses = counters.misses.load(std::memory_order_relaxed);
            result.coldHits = counters.hits.load(std::memory_order_relaxed);
        }
        else
            result.warmMilliseconds = std::min(result.warmMilliseconds, elapsed);

        result.warmMisses = counters.misses.load(std::memory_order_relaxed);
        result.warmHits = counters.hits.load(std::memory_order_relaxed);
        result.warmDirectReuses = counters.directReuses.load(std::memory_order_relaxed);
    }
    return result;
}

void ReportPool(char const* name, PoolMeasurement const& off, PoolMeasurement const& on)
{
    auto const warmSpeedup = on.warmMilliseconds > 0.0 ? off.warmMilliseconds / on.warmMilliseconds : 0.0;
    auto const coldSpeedup = on.coldMilliseconds > 0.0 ? off.coldMilliseconds / on.coldMilliseconds : 0.0;
    std::printf("%-38s %8.2f %8.2f %6.2fx  %9.2f %8.2f %6.2fx   SQLPrepare cold=%-4llu warm=%-4llu  hit=%-6llu "
                "direct=%llu\n",
                name,
                off.warmMilliseconds,
                on.warmMilliseconds,
                warmSpeedup,
                off.coldMilliseconds,
                on.coldMilliseconds,
                coldSpeedup,
                static_cast<unsigned long long>(on.coldMisses),
                static_cast<unsigned long long>(on.warmMisses),
                static_cast<unsigned long long>(on.warmHits),
                static_cast<unsigned long long>(on.warmDirectReuses));
}

/// The pool shapes compared below. `preparedStatementCacheCapacity` is a compile-time policy, so each
/// on/off pair is two distinct pool types rather than one type configured twice.
constexpr auto PoolCacheCapacity = PreparedStatementCacheCapacitySuggested;

constexpr auto SharedPoolOff = PoolConfig { .initialSize = 4, .maxSize = 4, .growthStrategy = GrowthStrategy::BoundedWait };
constexpr auto SharedPoolOn = PoolConfig { .initialSize = 4,
                                           .maxSize = 4,
                                           .growthStrategy = GrowthStrategy::BoundedWait,
                                           .preparedStatementCacheCapacity = PoolCacheCapacity };

constexpr auto SingleConnectionPoolOff =
    PoolConfig { .initialSize = 1, .maxSize = 1, .growthStrategy = GrowthStrategy::BoundedWait };
constexpr auto SingleConnectionPoolOn = PoolConfig { .initialSize = 1,
                                                     .maxSize = 1,
                                                     .growthStrategy = GrowthStrategy::BoundedWait,
                                                     .preparedStatementCacheCapacity = PoolCacheCapacity };

constexpr auto OverflowPoolOff =
    PoolConfig { .initialSize = 2, .maxSize = 2, .growthStrategy = GrowthStrategy::BoundedOverflow };
constexpr auto OverflowPoolOn = PoolConfig { .initialSize = 2,
                                             .maxSize = 2,
                                             .growthStrategy = GrowthStrategy::BoundedOverflow,
                                             .preparedStatementCacheCapacity = PoolCacheCapacity };

/// Runs the pooled comparisons. Each line is one pool shape measured with the cache off and on; the
/// cold column is the first repetition, which pays one SQLPrepare per connection per query text.
void RunPooledScenarios(size_t operations, size_t seedRows)
{
    std::printf("\n%-38s %8s %8s %7s  %9s %8s %7s   (cache enabled)\n",
                "pooled scenario (3 queries per op)",
                "warm off",
                "warm on",
                "speedup",
                "cold off",
                "cold on",
                "speedup");

    // One connection, one worker: the pool recycles the same warmed connection, so this is the best
    // case a pool can offer - and the yardstick the others are read against.
    ReportPool("1 conn, 1 worker",
               MeasurePool<SingleConnectionPoolOff>(1, operations, seedRows),
               MeasurePool<SingleConnectionPoolOn>(1, operations, seedRows));

    // Four workers sharing one connection: the cache is warmed once and every worker rides it, at the
    // price of serialising on the pool.
    ReportPool("1 conn, 4 workers (contended)",
               MeasurePool<SingleConnectionPoolOff>(4, operations, seedRows),
               MeasurePool<SingleConnectionPoolOn>(4, operations, seedRows));

    // Four workers, four connections: no contention, but nothing is shared either - the pool pays the
    // warm-up four times over. This is the shape a worker-per-request server has.
    ReportPool("4 conns, 4 workers",
               MeasurePool<SharedPoolOff>(4, operations, seedRows),
               MeasurePool<SharedPoolOn>(4, operations, seedRows));

    // Eight workers against a pool that idles at most two connections under BoundedOverflow: every
    // acquire past the idle set builds a connection that is destroyed on return, taking its warmed
    // cache with it. The cache cannot amortise anything it does not survive to reuse.
    ReportPool("2 idle conns, 8 workers (overflow)",
               MeasurePool<OverflowPoolOff>(8, operations, seedRows),
               MeasurePool<OverflowPoolOn>(8, operations, seedRows));
}

} // namespace

int main(int argc, char** argv)
{
    size_t const iterations = argc > 1 ? std::stoul(argv[1]) : 1000;
    std::string const connectionString =
        argc > 2 ? argv[2] : std::string { "DRIVER=SQLite3;Database=/tmp/lw-prepared-cache-bench.sqlite" };
    if (argc > 3)
        g_repetitions = std::stoul(argv[3]);
    auto const sections = std::string_view { argc > 4 ? argv[4] : "all" };
    bool const runSingle = sections == "all" || sections == "single";
    bool const runPool = sections == "all" || sections == "pool";
    size_t const seedRows = argc > 5 ? std::stoul(argv[5]) : 200;

    SqlConnection::SetDefaultConnectionString(SqlConnectionString { connectionString });
    SqlLogger::SetLogger(g_logger);

    auto dm = DataMapper {};
    auto& connection = dm.Connection();

    std::ignore = SqlStatement { connection }.ExecuteDirect(R"(DROP TABLE IF EXISTS "BenchCacheItem")");
    dm.CreateTable<Item>();
    {
        auto transaction = SqlTransaction { connection };
        for (size_t i = 0; i < seedRows; ++i)
        {
            auto item = Item { .name = SqlAnsiString<40> { std::format("item-{}", i) }, .value = static_cast<int32_t>(i) };
            dm.Create(item);
        }
        transaction.Commit();
    }

    auto const selectByIdSql = std::string { R"(SELECT "id", "name", "value" FROM "BenchCacheItem" WHERE "id" = ?)" };
    auto const interleaved = std::vector<std::string> {
        selectByIdSql,
        R"(SELECT "id", "name" FROM "BenchCacheItem" WHERE "value" = ?)",
        R"(SELECT "name", "value" FROM "BenchCacheItem" WHERE "id" >= ? AND "id" < 5)",
        R"(SELECT COUNT(*) FROM "BenchCacheItem" WHERE "id" <> ?)",
    };

    std::printf(
        "== prepared-statement cache: %zu iterations, %zu seed rows, best of %zu\n", iterations, seedRows, g_repetitions);
    std::printf("== %s (server: %s, reuse supported: %s)\n",
                connectionString.c_str(),
                std::format("{}", connection.ServerType()).c_str(),
                connection.SupportsPreparedStatementReuse() ? "yes" : "no");
    if (runSingle)
    {
        std::printf("%-44s %9s %9s   %7s   (cache enabled)\n", "scenario", "off/ms", "on/ms", "speedup");

        Report("fresh statement, prepare+execute+fetch", MeasureBoth(connection, [&] {
                   std::ignore = FreshStatementPerIteration(connection, selectByIdSql, iterations);
               }));

        Report("fresh statement, prepare only",
               MeasureBoth(connection, [&] { FreshStatementPrepareOnly(connection, selectByIdSql, iterations); }));

        Report("one statement, 4 interleaved queries",
               MeasureBoth(connection, [&] { std::ignore = InterleavedQueries(connection, interleaved, iterations); }));

        Report("DataMapper::QuerySingle by primary key", MeasureBoth(connection, [&] {
                   for (size_t i = 1; i <= iterations; ++i)
                       std::ignore = dm.QuerySingle<Item, DataMapperOptions { .loadRelations = false }>(
                           static_cast<uint64_t>(i % seedRows) + 1);
               }));

        // The query-builder read path builds a fresh SqlStatement per call (unlike QuerySingle above,
        // which reuses the mapper's own statement), so it is the DataMapper shape the cache can help.
        Report("DataMapper::Query<>().Where().All()", MeasureBoth(connection, [&] {
                   for (size_t i = 0; i < iterations; ++i)
                       std::ignore = dm.Query<Item, DataMapperOptions { .loadRelations = false }>()
                                         .Where(FieldNameOf<Member(Item::value)>, static_cast<int32_t>(i % seedRows))
                                         .All();
               }));

        Report("DataMapper::Create (INSERT + last id)", MeasureBoth(connection, [&] {
                   auto transaction = SqlTransaction { connection };
                   for (size_t i = 0; i < iterations; ++i)
                   {
                       auto item =
                           Item { .name = SqlAnsiString<40> { std::format("new-{}", i) }, .value = static_cast<int32_t>(i) };
                       dm.Create(item);
                   }
                   transaction.Rollback();
               }));
    }

    if (runPool)
        RunPooledScenarios(iterations, seedRows);
    return 0;
}
