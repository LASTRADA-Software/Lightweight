// SPDX-License-Identifier: Apache-2.0
//
// Tests that pin the behavior of the stdexec-backed executor internals (exec::static_thread_pool +
// exec::async_scope under ThreadPoolExecutor, and the serializing StrandExecutor layered over it).
// These complement ExecutorTests.cpp, focusing on the guarantees the stdexec rewrite must preserve:
// the destructor drain barrier, strand FIFO ordering, and strand-state lifetime.

#include <Lightweight/Async/StrandExecutor.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <latch>
#include <ranges>
#include <thread>
#include <vector>

using namespace Lightweight::Async;
using namespace std::chrono_literals;

TEST_CASE("Async.ThreadPoolExecutor destructor drains in-flight work", "[Async][Executor][stdexec]")
{
    // The destructor must wait for every spawned work item to finish (sync_wait(scope.on_empty())),
    // not just stop the pool. Post more work than there are threads, each item sleeping briefly so a
    // large batch is still in flight when the pool goes out of scope, and assert all of it ran.
    constexpr int Iterations = 64;
    std::atomic<int> completed { 0 };
    {
        ThreadPoolExecutor pool(2);
        for ([[maybe_unused]] auto const _: std::views::iota(0, Iterations))
        {
            pool.Post([&completed] {
                std::this_thread::sleep_for(1ms);
                completed.fetch_add(1, std::memory_order_relaxed);
            });
        }
        // pool destructor runs here: it must block until all Iterations items complete.
    }
    CHECK(completed.load() == Iterations);
}

TEST_CASE("Async.ThreadPoolExecutor reports the configured thread count", "[Async][Executor][stdexec]")
{
    CHECK(ThreadPoolExecutor { 1 }.ThreadCount() == 1);
    CHECK(ThreadPoolExecutor { 8 }.ThreadCount() == 8);
}

TEST_CASE("Async.StrandExecutor preserves FIFO order of posted work", "[Async][Executor][stdexec]")
{
    ThreadPoolExecutor pool(4);
    StrandExecutor strand { pool };

    // No lock around `order`: the strand guarantees the closures never run concurrently, and FIFO
    // means they run in the exact order posted. A sorted result therefore proves both properties.
    constexpr int Iterations = 1000;
    std::vector<int> order;
    order.reserve(Iterations);
    std::latch done { Iterations };
    for (auto const index: std::views::iota(0, Iterations))
    {
        strand.Post([&order, &done, index] {
            order.push_back(index);
            done.count_down();
        });
    }
    done.wait();

    REQUIRE(order.size() == static_cast<std::size_t>(Iterations));
    CHECK(std::ranges::is_sorted(order));
}

TEST_CASE("Async.StrandExecutor state outlives the wrapper while a drain is in flight", "[Async][Executor][stdexec]")
{
    // The strand's State is held by a shared_ptr that each scheduled drain closure copies, so work
    // already posted still completes even if the StrandExecutor wrapper is destroyed immediately
    // afterwards (e.g. SqlConnection::EnableAsync replacing a backend, or a transaction finalizing).
    ThreadPoolExecutor pool(2);
    constexpr int Iterations = 200;
    std::atomic<int> ran { 0 };
    std::latch done { Iterations };
    {
        StrandExecutor strand { pool };
        for ([[maybe_unused]] auto const _: std::views::iota(0, Iterations))
        {
            strand.Post([&ran, &done] {
                ran.fetch_add(1, std::memory_order_relaxed);
                done.count_down();
            });
        }
        // strand wrapper destroyed here; the in-flight drain closure keeps the shared State alive.
    }
    done.wait();
    CHECK(ran.load() == Iterations);
}
