// SPDX-License-Identifier: Apache-2.0

#include "AsyncTestUtils.hpp"

#include <Lightweight/Async/Async.hpp>
#include <Lightweight/Async/Backend.hpp>
#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/StrandExecutor.hpp>
#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/ThreadOffloadBackend.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <stop_token>
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

    int const result = RunPumped(
        [&]() -> Task<int> {
            int const value = co_await Async(strand, appLoop, [&]() -> int {
                workerThread = std::this_thread::get_id();
                return 21;
            });
            resumeThread = std::this_thread::get_id();
            co_return value * 2;
        },
        appLoop);

    CHECK(result == 42);
    CHECK(workerThread != mainThread); // the blocking closure ran on a worker thread
    CHECK(resumeThread == mainThread); // the coroutine resumed on the pumping (app) thread
}

TEST_CASE("Async.Async propagates exceptions from offloaded work", "[Async][Offload]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    StrandExecutor strand { dbWorkers };

    CHECK_THROWS_AS(RunPumped(
                        [&]() -> Task<int> {
                            co_return co_await Async(strand, appLoop, []() -> int { throw std::runtime_error("boom"); });
                        },
                        appLoop),
                    std::runtime_error);
}

TEST_CASE("Async.Async honors cancellation requested before dispatch", "[Async][Offload]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    StrandExecutor strand { dbWorkers };

    std::stop_source source;
    source.request_stop(); // cancel up-front
    auto token = source.get_token();

    bool ran = false;
    CHECK_THROWS_AS(
        RunPumped([&]() -> Task<void> { co_await Async(strand, appLoop, [&ran] { ran = true; }, token); }, appLoop),
        OperationCancelledError);
    CHECK_FALSE(ran); // cancelled before the closure ran
}

TEST_CASE("Async.Async pre-cancelled work is never dispatched to the offload executor", "[Async][Offload]")
{
    // Regression: a token already requested before the await must complete the operation WITHOUT
    // dispatching to the offload executor at all (so a pre-cancelled op never occupies a DB worker).
    // We offload to an executor that silently drops every work item: under the fixed behavior the
    // coroutine still resumes (via the resume scheduler) and throws OperationCancelledError, whereas
    // posting the work to this executor would drop it and hang forever.
    struct DroppingExecutor final: IExecutor
    {
        void Post(Work /*work*/) override {} // intentionally never runs anything
    };

    DroppingExecutor offload;
    ManualExecutor appLoop;

    std::stop_source source;
    source.request_stop(); // cancel up-front
    auto token = source.get_token();

    bool ran = false;
    CHECK_THROWS_AS(
        RunPumped([&]() -> Task<void> { co_await Async(offload, appLoop, [&ran] { ran = true; }, token); }, appLoop),
        OperationCancelledError);
    CHECK_FALSE(ran); // never dispatched, so the closure could not have run
}

TEST_CASE("Async.Async honors cancellation requested after dispatch but before the work runs", "[Async][Offload]")
{
    // The token is cancelled after dispatch (await_suspend's pre-check already passed) but before the
    // work runs, exercising the in-flight check in OffloadAwaitable::Run: it must throw without running _fn.
    struct CancelThenRunExecutor final: IExecutor
    {
        std::stop_source source;

        explicit CancelThenRunExecutor(std::stop_source cancelSource) noexcept:
            source { std::move(cancelSource) }
        {
        }

        void Post(Work work) override
        {
            source.request_stop();
            work();
        }
    };

    std::stop_source source;
    CancelThenRunExecutor offload { source }; // shares the stop-state, requests it from inside Post()
    auto token = source.get_token();
    ManualExecutor appLoop;

    bool ran = false;
    CHECK_THROWS_AS(
        RunPumped([&]() -> Task<void> { co_await Async(offload, appLoop, [&ran] { ran = true; }, token); }, appLoop),
        OperationCancelledError);
    CHECK_FALSE(ran); // Run() threw on the in-flight check before calling the user closure
}

TEST_CASE("Async.Async with a non-cancellable default stop_token runs the work normally", "[Async][Offload]")
{
    // A default-constructed std::stop_token has no associated stop-state (stop_requested() is always
    // false), so it must never short-circuit the offloaded work — the cheap default for callers that
    // do not need cancellation.
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    StrandExecutor strand { dbWorkers };

    bool ran = false;
    int const result = RunPumped(
        [&]() -> Task<int> {
            co_return co_await Async(
                strand,
                appLoop,
                [&ran]() -> int {
                    ran = true;
                    return 5;
                },
                std::stop_token {});
        },
        appLoop);

    CHECK(ran);
    CHECK(result == 5);
}

TEST_CASE("Async.RunAsync via ThreadOffloadBackend", "[Async][Offload]")
{
    ThreadPoolExecutor dbWorkers { 2 };
    ManualExecutor appLoop;
    ThreadOffloadBackend backend { dbWorkers, appLoop };

    CHECK(RunPumped([&]() -> Task<int> { co_return co_await RunAsync(backend, [] { return 100; }); }, appLoop) == 100);
}

TEST_CASE("Async.SyncWaitPumping completes when the result lands on another thread", "[Async][Offload]")
{
    // Resume on the worker pool (NOT the pumped ManualExecutor), so the driver's completion Set()
    // runs on a pool thread. Without the SyncWaitEvent waker, the pumping thread would sleep
    // forever on a flag that was set without notifying its condition variable.
    ThreadPoolExecutor dbWorkers { 2 };
    StrandExecutor strand { dbWorkers };
    ManualExecutor appLoop;

    int const result =
        RunPumped([&]() -> Task<int> { co_return co_await Async(strand, dbWorkers, [] { return 7; }); }, appLoop);
    CHECK(result == 7);
}
