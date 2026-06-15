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
    // already posted still completes even if the StrandExecutor wrapper is destroyed while the drain
    // is still running (e.g. SqlConnection::EnableAsync replacing a backend, or a transaction
    // finalizing). To actually exercise that lifetime-extension path (and not merely have the drain
    // finish before the wrapper is destroyed), the first posted closure blocks on `release` until
    // after the wrapper has been destroyed, pinning the drain in flight across destruction.
    ThreadPoolExecutor pool(2);
    constexpr int Iterations = 200;
    std::atomic<int> ran { 0 };
    std::latch started { 1 }; ///< the gating drain closure has begun running
    std::latch release { 1 }; ///< opened only after the wrapper is destroyed
    std::latch done { Iterations };
    {
        StrandExecutor strand { pool };
        strand.Post([&] {
            started.count_down();
            release.wait(); // keep this drain in flight until the wrapper below is gone
            ran.fetch_add(1, std::memory_order_relaxed);
            done.count_down();
        });
        for ([[maybe_unused]] auto const _: std::views::iota(1, Iterations))
        {
            strand.Post([&ran, &done] {
                ran.fetch_add(1, std::memory_order_relaxed);
                done.count_down();
            });
        }
        started.wait(); // ensure the drain closure is running before we destroy the wrapper
        // strand wrapper destroyed here, with the drain still blocked in `release.wait()`; the
        // in-flight drain closure's shared_ptr copy must keep State alive for the remaining items.
    }
    release.count_down(); // let the pinned drain (and the rest of the FIFO) complete
    done.wait();
    CHECK(ran.load() == Iterations);
}
