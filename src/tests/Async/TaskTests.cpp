// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/Task.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

using namespace Lightweight;

namespace
{

Async::Task<int> ReturnFortyTwo()
{
    co_return 42;
}

Async::Task<int> AddOne(int value)
{
    co_return value + 1;
}

Async::Task<int> Chained()
{
    auto const a = co_await ReturnFortyTwo();
    auto const b = co_await AddOne(a);
    co_return b;
}

Async::Task<std::string> ReturnMoveOnlyish()
{
    co_return std::string(1000, 'x');
}

Async::Task<void> ThrowVoid()
{
    throw std::runtime_error("boom-void");
    co_return;
}

Async::Task<int> ThrowInt()
{
    throw std::runtime_error("boom-int");
    co_return 0;
}

Async::Task<int> DeepChain(int depth)
{
    if (depth == 0)
        co_return 0;
    co_return 1 + co_await DeepChain(depth - 1);
}

} // namespace

TEST_CASE("Async.Task: returns a value", "[Async][Task]")
{
    CHECK(Async::SyncWait(ReturnFortyTwo()) == 42);
}

TEST_CASE("Async.Task: composes via co_await", "[Async][Task]")
{
    CHECK(Async::SyncWait(Chained()) == 43);
}

TEST_CASE("Async.Task: returns a large/move-only-ish value", "[Async][Task]")
{
    auto const result = Async::SyncWait(ReturnMoveOnlyish());
    CHECK(result.size() == 1000);
}

TEST_CASE("Async.Task<void>: completes", "[Async][Task]")
{
    CHECK_NOTHROW(Async::SyncWait([]() -> Async::Task<void> {
        co_return;
    }()));
}

TEST_CASE("Async.Task: propagates exceptions across co_await", "[Async][Task]")
{
    CHECK_THROWS_AS(Async::SyncWait(ThrowVoid()), std::runtime_error);
    CHECK_THROWS_AS(Async::SyncWait(ThrowInt()), std::runtime_error);
}

TEST_CASE("Async.Task: deep symmetric-transfer chain does not overflow the stack", "[Async][Task]")
{
    // If symmetric transfer were not used, this would overflow the native stack.
    CHECK(Async::SyncWait(DeepChain(100'000)) == 100'000);
}

TEST_CASE("Async.Task: lazy — body does not run until awaited", "[Async][Task]")
{
    bool ran = false;
    // Name the closure so it outlives the coroutine: a temporary lambda-coroutine that captures
    // would dangle once destroyed, because its body runs later (inside SyncWait).
    auto makeTask = [&ran]() -> Async::Task<int> {
        ran = true;
        co_return 7;
    };
    auto task = makeTask();
    CHECK_FALSE(ran); // lazy: nothing has executed yet
    auto const result = Async::SyncWait(std::move(task));
    CHECK(result == 7);
    CHECK(ran);
}
