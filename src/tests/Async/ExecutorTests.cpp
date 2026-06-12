// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/StrandExecutor.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <latch>
#include <ranges>
#include <stdexcept>
#include <vector>

using namespace Lightweight::Async;

TEST_CASE("Async.InlineExecutor runs work synchronously", "[Async][Executor]")
{
    InlineExecutor executor;
    int value = 0;
    executor.Post([&value] { value = 5; });
    CHECK(value == 5);
}

TEST_CASE("Async.ThreadPoolExecutor runs posted work", "[Async][Executor]")
{
    ThreadPoolExecutor pool(4);
    CHECK(pool.ThreadCount() == 4);

    std::atomic<int> counter { 0 };
    std::latch done { 100 };
    for ([[maybe_unused]] auto const _: std::views::iota(0, 100))
    {
        pool.Post([&counter, &done] {
            counter.fetch_add(1, std::memory_order_relaxed);
            done.count_down();
        });
    }
    done.wait();
    CHECK(counter.load() == 100);
}

TEST_CASE("Async.StrandExecutor serializes work over a thread pool", "[Async][Executor]")
{
    ThreadPoolExecutor pool(4);
    StrandExecutor strand { pool };

    // Deliberately a NON-atomic counter: correctness here proves the strand serializes,
    // i.e. no two work items run concurrently (also a clean run under ThreadSanitizer).
    int counter = 0;
    constexpr int Iterations = 2000;
    std::latch done { Iterations };
    for ([[maybe_unused]] auto const _: std::views::iota(0, Iterations))
    {
        strand.Post([&counter, &done] {
            ++counter;
            done.count_down();
        });
    }
    done.wait();
    CHECK(counter == Iterations);
}

TEST_CASE("Async.ManualExecutor pumps work in FIFO order", "[Async][Executor]")
{
    ManualExecutor executor;
    std::vector<int> order;

    executor.Post([&order] { order.push_back(1); });
    executor.Post([&order] { order.push_back(2); });
    CHECK(executor.PendingCount() == 2);

    CHECK(executor.RunOne());
    CHECK(order == std::vector { 1 });

    CHECK(executor.Drain() == 1);
    CHECK(order == std::vector { 1, 2 });

    CHECK_FALSE(executor.RunOne());
    CHECK(executor.Drain() == 0);
}

TEST_CASE("Async.ManualExecutor RunUntil stops on predicate", "[Async][Executor]")
{
    ManualExecutor executor;
    bool flag = false;
    executor.Post([&flag] { flag = true; });
    executor.RunUntil([&flag] { return flag; });
    CHECK(flag);
}

TEST_CASE("Async.ManualExecutor RunUntil pumps to completion even after Stop", "[Async][Executor]")
{
    // Regression: RunUntil must ignore _stopped and pump until the predicate is satisfied, so a
    // SyncWaitPumping over a stopped executor never returns an unfinished result.
    ManualExecutor executor;
    executor.Stop(); // sets _stopped (intended for Run(), not RunUntil)
    bool done = false;
    executor.Post([&done] { done = true; });
    executor.RunUntil([&done] { return done; });
    CHECK(done);
}

TEST_CASE("Async.ThreadPoolExecutor rejects a zero thread count", "[Async][Executor]")
{
    // A pool with no workers would silently never run posted work; reject it loudly instead.
    CHECK_THROWS_AS(ThreadPoolExecutor { 0 }, std::invalid_argument);
}
