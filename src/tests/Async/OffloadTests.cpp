// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/Async/Async.hpp>
#include <Lightweight/Async/Backend.hpp>
#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/StrandExecutor.hpp>
#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/ThreadOffloadBackend.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <thread>

using namespace Lightweight::Async;

TEST_CASE("Async.Async offloads work and resumes on the app thread", "[Async][Offload]")
{
    ThreadPoolExecutor dbWorkers { 2 };
    ManualExecutor appLoop;
    StrandExecutor strand { dbWorkers };

    auto const mainThread = std::this_thread::get_id();
    std::thread::id workerThread {};
    std::thread::id resumeThread {};

    auto task = [&]() -> Task<int> {
        int const value = co_await Async(strand, appLoop, [&]() -> int {
            workerThread = std::this_thread::get_id();
            return 21;
        });
        resumeThread = std::this_thread::get_id();
        co_return value * 2;
    }();

    int const result = SyncWaitPumping(std::move(task), appLoop);

    CHECK(result == 42);
    CHECK(workerThread != mainThread); // the blocking closure ran on a worker thread
    CHECK(resumeThread == mainThread); // the coroutine resumed on the pumping (app) thread
}

TEST_CASE("Async.Async propagates exceptions from offloaded work", "[Async][Offload]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    StrandExecutor strand { dbWorkers };

    auto task = [&]() -> Task<int> {
        co_return co_await Async(strand, appLoop, []() -> int { throw std::runtime_error("boom"); });
    }();

    CHECK_THROWS_AS(SyncWaitPumping(std::move(task), appLoop), std::runtime_error);
}

TEST_CASE("Async.Async honors cancellation requested before dispatch", "[Async][Offload]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    StrandExecutor strand { dbWorkers };

    auto token = CancellationToken::Create();
    token.Request(); // cancel up-front

    bool ran = false;
    auto task = [&]() -> Task<void> {
        co_await Async(strand, appLoop, [&ran] { ran = true; }, token);
    }();

    CHECK_THROWS_AS(SyncWaitPumping(std::move(task), appLoop), OperationCancelledError);
    CHECK_FALSE(ran); // cancelled before the closure ran
}

TEST_CASE("Async.RunAsync via ThreadOffloadBackend", "[Async][Offload]")
{
    ThreadPoolExecutor dbWorkers { 2 };
    ManualExecutor appLoop;
    ThreadOffloadBackend backend { dbWorkers, appLoop };
    CHECK_FALSE(backend.IsNative());

    auto task = [&]() -> Task<int> {
        co_return co_await RunAsync(backend, [] { return 100; });
    }();

    CHECK(SyncWaitPumping(std::move(task), appLoop) == 100);
}
