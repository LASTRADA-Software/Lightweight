// SPDX-License-Identifier: Apache-2.0
//
// Runtime benchmark for the prepared-statement cache (issue #552).
//
// Measures wall-clock time of workloads that re-prepare the same SQL text, with the connection's
// prepared-statement cache disabled (the default) and enabled, so the feature can be judged on
// numbers rather than on the expectation that skipping SQLPrepare must be faster.
//
//     LightweightPreparedStatementCacheBenchmark [iterations] [connectionString] [repetitions]
//
// The reported prepare counts come from a SqlLogger subclass and from the cache's own hit/miss
// counters, so a scenario that accidentally stops exercising the cache shows up as a count rather
// than as a suspiciously small delta.

#include <Lightweight/Lightweight.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <span>
#include <string>
#include <string_view>
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
    size_t prepares = 0;

    void OnPrepare(std::string_view const& /*query*/) override
    {
        ++prepares;
    }

    void Reset() noexcept
    {
        prepares = 0;
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

} // namespace

int main(int argc, char** argv)
{
    size_t const iterations = argc > 1 ? std::stoul(argv[1]) : 1000;
    std::string const connectionString =
        argc > 2 ? argv[2] : std::string { "DRIVER=SQLite3;Database=/tmp/lw-prepared-cache-bench.sqlite" };
    if (argc > 3)
        g_repetitions = std::stoul(argv[3]);
    size_t const seedRows = 200;

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

    return 0;
}
