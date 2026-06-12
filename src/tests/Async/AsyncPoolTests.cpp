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

#include <catch2/catch_test_macros.hpp>

#include <optional>

using namespace Lightweight;
using namespace Lightweight::Async;

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
