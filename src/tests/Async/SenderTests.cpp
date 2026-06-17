// SPDX-License-Identifier: Apache-2.0
//
// Tests for the Task -> stdexec sender bridge (Async::AsSender, <Lightweight/Async/Sender.hpp>).
// They pin the mapping of a Lightweight coroutine's outcome onto stdexec's completion channels:
//   produced value  -> set_value
//   OperationCancelledError -> set_stopped
//   any other exception     -> set_error (std::exception_ptr)
// and that the wrapped Task stays lazy until the sender is started. A final case drives a real
// async DB round-trip through a sender pipeline.

// clang-format off
#include "../Utils.hpp" // must precede Entities.hpp, which uses the Member() macro defined there
#include "../DataMapper/Entities.hpp"
// clang-format on

#include "AsyncTestUtils.hpp"

#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/Sender.hpp>
#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/Task.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <optional>
#include <stdexcept>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

using namespace Lightweight;
using namespace Lightweight::Async;

namespace
{

/// Unwraps an stdexec::sync_wait result tuple, throwing when the operation completed via set_stopped
/// (a disengaged optional). TaskSender advertises a stopped channel, so sync_wait yields a nullable
/// optional; this gives a checked access that fails the test loudly instead of dereferencing blindly.
template <typename Tuple>
Tuple UnwrapSyncWait(std::optional<Tuple> result)
{
    if (!result.has_value())
        throw std::logic_error { "UnwrapSyncWait: sync_wait completed via set_stopped" };
    return std::move(result).value();
}

Task<int> MakeValueTask(int value)
{
    co_return value;
}

Task<void> MakeVoidTask(std::atomic<bool>* ran)
{
    ran->store(true);
    co_return;
}

Task<int> MakeThrowingTask()
{
    throw std::runtime_error { "boom" };
    co_return 0; // unreachable; makes this a coroutine
}

Task<int> MakeCancelledTask()
{
    throw OperationCancelledError {};
    co_return 0; // unreachable; makes this a coroutine
}

} // namespace

TEST_CASE("Async.Sender: value Task pipes through then and sync_wait", "[Async][Sender][stdexec]")
{
    auto work = AsSender(MakeValueTask(20)) | stdexec::then([](int v) { return v + 1; });
    // sync_wait returns std::optional<std::tuple<...>>, disengaged only on set_stopped. TaskSender
    // advertises a stopped channel, so the optional is genuinely nullable to the type system; unwrap
    // it through the test helper that throws (and thus guards the access) when disengaged.
    auto const [value] = UnwrapSyncWait(stdexec::sync_wait(std::move(work)));
    CHECK(value == 21);
}

TEST_CASE("Async.Sender: void Task completes the value channel", "[Async][Sender][stdexec]")
{
    std::atomic<bool> ran { false };
    auto const result = stdexec::sync_wait(AsSender(MakeVoidTask(&ran)));
    CHECK(result.has_value()); // a completed (non-stopped, non-error) void sender
    CHECK(ran.load());
}

TEST_CASE("Async.Sender: the wrapped Task is lazy until started", "[Async][Sender][stdexec]")
{
    std::atomic<bool> ran { false };
    auto sender = AsSender(MakeVoidTask(&ran));
    CHECK_FALSE(ran.load()); // connecting/holding the sender must not run the Task
    stdexec::sync_wait(std::move(sender));
    CHECK(ran.load());
}

TEST_CASE("Async.Sender: a thrown exception completes the error channel", "[Async][Sender][stdexec]")
{
    // sync_wait rethrows whatever flowed down the error channel; for us that is the exception_ptr.
    CHECK_THROWS_AS(stdexec::sync_wait(AsSender(MakeThrowingTask())), std::runtime_error);
}

TEST_CASE("Async.Sender: OperationCancelledError maps to the stopped channel", "[Async][Sender][stdexec]")
{
    // set_stopped surfaces through sync_wait as a disengaged optional (not an exception).
    auto const result = stdexec::sync_wait(AsSender(MakeCancelledTask()));
    CHECK_FALSE(result.has_value());
}

TEST_CASE("Async.Sender: when_all over two senders on a thread pool", "[Async][Sender][stdexec]")
{
    auto pool = exec::static_thread_pool(2);
    auto sched = pool.get_scheduler();

    auto work = stdexec::when_all(stdexec::starts_on(sched, AsSender(MakeValueTask(2))),
                                  stdexec::starts_on(sched, AsSender(MakeValueTask(40))));
    auto const [a, b] = UnwrapSyncWait(stdexec::sync_wait(std::move(work)));
    CHECK(a + b == 42);
}

TEST_CASE_METHOD(SqlTestFixture, "Async.Sender: async DB round-trip via a sender pipeline", "[Async][Sender][DataMapper]")
{
    // Resume the DB coroutines on the worker pool itself (the multi-threaded model), so a blocking
    // stdexec::sync_wait on this thread does not have to pump anything — it just blocks until the
    // pipeline completes off-thread.
    ThreadPoolExecutor dbWorkers { 2 };

    DataMapper dm;
    dm.Connection().EnableAsync(dbWorkers, dbWorkers);
    dm.CreateTables<Person>();

    auto person = Person { .id = SqlGuid::Create(), .name = "Sender", .age = 21 };

    // INSERT off-thread, then map the returned primary key through a sender `then`.
    auto const [createdId] =
        UnwrapSyncWait(stdexec::sync_wait(AsSender(dm.CreateAsync(person)) | stdexec::then([](auto id) { return id; })));
    CHECK(createdId == person.id.Value());

    auto const [loaded] = UnwrapSyncWait(stdexec::sync_wait(AsSender(dm.QuerySingleAsync<Person>(person.id.Value()))));
    CHECK(RequireValue(loaded).name == person.name);
}
