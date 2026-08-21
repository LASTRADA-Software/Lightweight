// SPDX-License-Identifier: Apache-2.0

// clang-format off
#include "../Utils.hpp" // must precede Entities.hpp, which uses the Member() macro defined there
#include "../DataMapper/Entities.hpp"
// clang-format on

#include "AsyncTestUtils.hpp"

#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/Task.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/DataMapper/Pool.hpp>
#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlLogger.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace Lightweight;
using namespace Lightweight::Async;

namespace
{

/// Counts SqlLogger::OnConnectionIdle / OnConnectionReuse invocations, leaving every other hook a no-op.
class CapturingConnectionLogger: public SqlLogger::Null
{
  public:
    int idleCount = 0;
    int reuseCount = 0;

    void OnConnectionIdle(SqlConnection const& /*connection*/) override
    {
        ++idleCount;
    }
    void OnConnectionReuse(SqlConnection const& /*connection*/) override
    {
        ++reuseCount;
    }
};

/// RAII helper that installs a replacement SqlLogger for the scope's lifetime and restores the
/// previous one on destruction, mirroring the pattern used in SqlLoggerTests.cpp.
struct LoggerSwap
{
    SqlLogger* previous;
    explicit LoggerSwap(SqlLogger& replacement):
        previous { &SqlLogger::GetLogger() }
    {
        SqlLogger::SetLogger(replacement);
    }
    ~LoggerSwap()
    {
        SqlLogger::SetLogger(*previous);
    }
    LoggerSwap(LoggerSwap const&) = delete;
    LoggerSwap& operator=(LoggerSwap const&) = delete;
    LoggerSwap(LoggerSwap&&) = delete;
    LoggerSwap& operator=(LoggerSwap&&) = delete;
};

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "Async.Pool: AcquireAsync acquires, queries and returns", "[Async][Pool]")
{
    ThreadPoolExecutor dbWorkers { 2 };
    ManualExecutor appLoop;
    auto pool = Pool<PoolConfig { .initialSize = 2, .maxSize = 4, .growthStrategy = GrowthStrategy::BoundedOverflow }>();

    auto const id = SqlGuid::Create();
    {
        DataMapper dm;
        dm.CreateTables<Person>();
        auto person = Person { .id = id, .name = "Bob", .age = 42 };
        dm.Create(person);
    }

    auto const result = RunPumped(
        [&]() -> Task<std::optional<Person>> {
            auto dm = co_await pool.AcquireAsync(dbWorkers, appLoop);
            co_return co_await dm->QuerySingleAsync<Person>(id);
        },
        appLoop);
    REQUIRE(result.has_value());
    CHECK(pool.IdleCount() == 2); // the acquired mapper was returned to the pool
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Async.Pool: AcquireAsync applies the pool's prepared-statement cache capacity",
                 "[Async][Pool][SqlPreparedStatementCache]")
{
    ThreadPoolExecutor dbWorkers { 2 };
    ManualExecutor appLoop;

    // initialSize = 0, so the awaitable has no idle entry to hand out and must create the connection
    // itself — a creation path of its own, which has to configure the connection like the others do.
    constexpr auto CachingPoolConfig = PoolConfig {
        .initialSize = 0,
        .maxSize = 4,
        .growthStrategy = GrowthStrategy::BoundedOverflow,
        .preparedStatementCacheCapacity = PreparedStatementCacheCapacitySuggested,
    };
    auto pool = Pool<CachingPoolConfig>();
    REQUIRE(pool.IdleCount() == 0);

    auto const capacity = RunPumped(
        [&]() -> Task<std::size_t> {
            auto dm = co_await pool.AcquireAsync(dbWorkers, appLoop);
            co_return dm->Connection().PreparedStatementCacheCapacity();
        },
        appLoop);

    CHECK(capacity == PreparedStatementCacheCapacitySuggested);
    CHECK(pool.IdleCount() == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Async.Pool: SetAsyncExecutors lets the no-argument AcquireAsync wire mappers",
                 "[Async][Pool]")
{
    ThreadPoolExecutor dbWorkers { 2 };
    ManualExecutor appLoop;
    auto pool = Pool<PoolConfig { .initialSize = 2, .maxSize = 4, .growthStrategy = GrowthStrategy::BoundedOverflow }>();

    // Configure the executors once; callers then use the no-argument AcquireAsync().
    pool.SetAsyncExecutors(dbWorkers, appLoop);

    auto const id = SqlGuid::Create();
    {
        DataMapper dm;
        dm.CreateTables<Person>();
        auto person = Person { .id = id, .name = "Carol", .age = 41 };
        dm.Create(person);
    }

    auto const result = RunPumped(
        [&]() -> Task<std::optional<Person>> {
            auto dm = co_await pool.AcquireAsync(); // uses the pool's configured executors
            CHECK(dm->Connection().IsAsyncEnabled());
            co_return co_await dm->QuerySingleAsync<Person>(id);
        },
        appLoop);
    REQUIRE(result.has_value());
    CHECK(pool.IdleCount() == 2); // the acquired mapper was returned to the pool
}

TEST_CASE_METHOD(SqlTestFixture, "Async.Pool: no-argument AcquireAsync without SetAsyncExecutors throws", "[Async][Pool]")
{
    auto pool = Pool<PoolConfig { .initialSize = 1, .maxSize = 2, .growthStrategy = GrowthStrategy::BoundedOverflow }>();

    // The no-argument overload is a precondition-checking factory: it throws before producing a Task
    // when no executors have been configured, rather than handing back an unusable coroutine.
    CHECK_THROWS_AS((void) pool.AcquireAsync(), std::logic_error);
}

TEST_CASE_METHOD(SqlTestFixture, "Async.Pool: AcquireAsync suspends until a mapper is returned", "[Async][Pool]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    auto pool = Pool<PoolConfig { .initialSize = 1, .maxSize = 1, .growthStrategy = GrowthStrategy::BoundedWait }>();

    // Check out the only mapper synchronously, exhausting the pool.
    std::optional holder { pool.Acquire() };
    CHECK(pool.IdleCount() == 0);

    bool acquired = false;

    // Releasing the held mapper while the coroutine is suspended hands it straight to the waiter.
    appLoop.Post([&holder] { holder.reset(); });

    RunPumped(
        [&]() -> Task<void> {
            auto dm = co_await pool.AcquireAsync(dbWorkers, appLoop);
            acquired = true;
            co_return;
        },
        appLoop);
    CHECK(acquired);
    CHECK(pool.IdleCount() == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "Async.Pool: a returned mapper is async-disabled", "[Async][Pool]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    auto pool = Pool<PoolConfig { .initialSize = 1, .maxSize = 2, .growthStrategy = GrowthStrategy::BoundedOverflow }>();

    RunPumped(
        [&]() -> Task<void> {
            auto dm = co_await pool.AcquireAsync(dbWorkers, appLoop);
            CHECK(dm->Connection().IsAsyncEnabled());
            co_return;
        },
        appLoop);

    // On return, the pool must clear the backend so the recycled connection does not retain
    // references to executors that may later be destroyed.
    auto reused = pool.Acquire();
    CHECK_FALSE(reused.Get().Connection().IsAsyncEnabled());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Async.Pool: destroying a parked AcquireAsync task de-registers its waiter",
                 "[Async][Pool]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    auto pool = Pool<PoolConfig { .initialSize = 1, .maxSize = 1, .growthStrategy = GrowthStrategy::BoundedWait }>();

    std::optional holder { pool.Acquire() }; // exhaust the pool
    CHECK(pool.IdleCount() == 0);

    {
        // Drive an AcquireAsync to its parked (suspended) state, then drop it without ever
        // returning a mapper. The awaitable destructor must remove the waiter, so the Return()
        // below does not write through a dangling slot / resume a destroyed coroutine.
        auto task = pool.AcquireAsync(dbWorkers, appLoop);
        task.GetHandle().resume();     // run the body until it parks on the exhausted pool
        REQUIRE_FALSE(task.IsReady()); // still suspended (parked), not completed
    } // task destroyed here -> waiter de-registered

    holder.reset(); // with the waiter gone, this just idles the mapper
    CHECK(pool.IdleCount() == 1);

    // The pool is still fully usable.
    {
        auto reused = pool.Acquire();
        CHECK(pool.IdleCount() == 0);
    }
    CHECK(pool.IdleCount() == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Async.Pool: dropping a parked task after hand-off reclaims its mapper (no capacity leak)",
                 "[Async][Pool]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    auto pool = Pool<PoolConfig { .initialSize = 1, .maxSize = 1, .growthStrategy = GrowthStrategy::BoundedWait }>();

    std::optional holder { pool.Acquire() }; // exhaust the pool (checked-out == maxSize)
    CHECK(pool.IdleCount() == 0);

    {
        auto task = pool.AcquireAsync(dbWorkers, appLoop);
        task.GetHandle().resume();     // park on the exhausted pool
        REQUIRE_FALSE(task.IsReady()); // suspended (parked)

        // Hand the mapper to the parked waiter. Return() schedules the resumption on appLoop but we
        // deliberately do NOT pump it, so await_resume never runs: the task is dropped while it has
        // already been handed (but not consumed) a mapper.
        holder.reset();
        REQUIRE_FALSE(task.IsReady()); // resumption still queued, not executed
    } // task destroyed while "fulfilled" -> destructor must reclaim the handed-off mapper

    // The handed-off mapper is reclaimed and the BoundedWait checked-out count released, so the pool
    // is usable again. (Before the fix the count leaked and the Acquire() below would block forever.)
    REQUIRE(pool.IdleCount() == 1);
    {
        auto reused = pool.Acquire();
        CHECK(pool.IdleCount() == 0);
    }
    CHECK(pool.IdleCount() == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Async.Pool: a blocking Acquire parked before an async waiter is served first (FIFO fairness)",
                 "[Async][Pool]")
{
    // Regression for async-over-sync starvation: a sync Acquire() parked BEFORE an AcquireAsync() waiter
    // must be served first (FIFO). Before the fix async waiters were always preferred and the blocked
    // Acquire() was never notified, so it could wait forever.
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    auto pool = Pool<PoolConfig { .initialSize = 1, .maxSize = 1, .growthStrategy = GrowthStrategy::BoundedWait }>();

    std::optional holder { pool.Acquire() }; // exhaust the pool (checked-out == maxSize)
    CHECK(pool.IdleCount() == 0);

    // (1) A synchronous Acquire() on a background thread parks FIRST.
    std::atomic<bool> syncAcquired { false };
    std::thread blocking { [&] {
        auto mapper = pool.Acquire(); // blocks until a mapper is handed to it
        syncAcquired.store(true, std::memory_order_release);
        // `mapper` is returned to the pool at scope exit, which then fulfills the async waiter below.
    } };
    while (pool.WaiterCount() < 1) // deterministically wait until the sync Acquire() has parked
        std::this_thread::yield();

    // (2) An AcquireAsync() waiter parks SECOND, so the FIFO order is [sync, async].
    auto asyncTask = pool.AcquireAsync(dbWorkers, appLoop);
    asyncTask.GetHandle().resume();     // run the body until it parks on the exhausted pool
    REQUIRE_FALSE(asyncTask.IsReady()); // suspended (parked)
    REQUIRE(pool.WaiterCount() == 2);

    // (3) Return the held mapper. FIFO hands it to the EARLIER (synchronous) waiter, not the async one.
    holder.reset();
    blocking.join(); // returns only once the synchronous Acquire() was actually served
    CHECK(syncAcquired.load(std::memory_order_acquire));

    // The sync waiter's released mapper fulfilled the async waiter; drive its resumption to finish cleanly.
    appLoop.Drain();
    REQUIRE(asyncTask.IsReady());
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: Return() reports an idled mapper via SqlLogger::OnConnectionIdle", "[Pool]")
{
    CapturingConnectionLogger capture;
    LoggerSwap const swap { capture };

    // initialSize = 0 so Acquire() below constructs a fresh mapper rather than reusing a pre-populated one.
    auto pool = Pool<PoolConfig { .initialSize = 0, .maxSize = 4, .growthStrategy = GrowthStrategy::BoundedOverflow }>();
    CHECK(capture.idleCount == 0);

    {
        auto mapper = pool.Acquire();
        CHECK(capture.idleCount == 0);
        CHECK(capture.reuseCount == 0); // fresh construction, not a reuse
    } // returned here -> parked back into the idle list (no waiter to hand off to)

    CHECK(capture.idleCount == 1);
    CHECK(capture.reuseCount == 0);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Pool: Acquire() reports handing back an idle mapper via SqlLogger::OnConnectionReuse",
                 "[Pool]")
{
    CapturingConnectionLogger capture;
    LoggerSwap const swap { capture };

    // initialSize = 1 pre-populates one idle mapper, so this first Acquire() is already a reuse.
    auto pool = Pool<PoolConfig { .initialSize = 1, .maxSize = 4, .growthStrategy = GrowthStrategy::BoundedOverflow }>();

    {
        auto mapper = pool.Acquire();
    } // reuses the pre-populated mapper, then idles it again on return
    CHECK(capture.idleCount == 1);
    CHECK(capture.reuseCount == 1);

    {
        auto mapper = pool.Acquire();
    } // reuses it again
    CHECK(capture.reuseCount == 2);
    CHECK(capture.idleCount == 2); // returned again -> idled a second time
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: Acquire() creating a fresh mapper does not report a reuse", "[Pool]")
{
    CapturingConnectionLogger capture;
    LoggerSwap const swap { capture };

    // initialSize = 0 so the very first Acquire() must construct a fresh DataMapper, not reuse one.
    auto pool = Pool<PoolConfig { .initialSize = 0, .maxSize = 4, .growthStrategy = GrowthStrategy::BoundedOverflow }>();

    auto mapper = pool.Acquire();
    CHECK(capture.reuseCount == 0);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Pool: BoundedWait idle-park path (ReturnLocked, no waiter) reports OnConnectionIdle",
                 "[Pool]")
{
    CapturingConnectionLogger capture;
    LoggerSwap const swap { capture };

    auto pool = Pool<PoolConfig { .initialSize = 1, .maxSize = 1, .growthStrategy = GrowthStrategy::BoundedWait }>();

    {
        auto mapper = pool.Acquire();
    } // no parked waiter -> ReturnLocked idles it

    CHECK(capture.idleCount == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Pool: BoundedWait hand-off to a parked waiter is a reuse, not an idle transition",
                 "[Pool]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    CapturingConnectionLogger capture;
    LoggerSwap const swap { capture };

    auto pool = Pool<PoolConfig { .initialSize = 1, .maxSize = 1, .growthStrategy = GrowthStrategy::BoundedWait }>();

    std::optional holder { pool.Acquire() }; // exhaust the pool: reuses the pre-populated mapper
    CHECK(capture.idleCount == 0);
    CHECK(capture.reuseCount == 1);

    auto task = pool.AcquireAsync(dbWorkers, appLoop);
    task.GetHandle().resume();     // park on the exhausted pool
    REQUIRE_FALSE(task.IsReady()); // suspended (parked)

    // Returning the held mapper hands it directly to the parked waiter (FIFO): a reuse, never an idle
    // transition, since the mapper is never placed into _idleDataMappers.
    holder.reset();
    CHECK(capture.idleCount == 0);
    CHECK(capture.reuseCount == 2);

    appLoop.Drain();
    REQUIRE(task.IsReady());
}
