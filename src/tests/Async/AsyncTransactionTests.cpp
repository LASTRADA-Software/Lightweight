// SPDX-License-Identifier: Apache-2.0

// clang-format off
#include "../Utils.hpp" // must precede Entities.hpp, which uses the Member() macro defined there
#include "../DataMapper/Entities.hpp"
// clang-format on

#include "AsyncTestUtils.hpp"

#include <Lightweight/Async/AsyncSqlTransaction.hpp>
#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/Task.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using namespace Lightweight;
using namespace Lightweight::Async;

TEST_CASE_METHOD(SqlTestFixture, "Async.Transaction: committed work is persisted", "[Async][Transaction]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    DataMapper dm;
    dm.Connection().EnableAsync(dbWorkers, appLoop);
    dm.CreateTables<Person>();

    auto const id = SqlGuid::Create();
    RunPumped(
        [&]() -> Task<void> {
            AsyncSqlTransaction tx { dm.Connection() };
            co_await tx.BeginAsync();
            auto person = Person { .id = id, .name = "Committed", .age = 1 };
            co_await dm.CreateAsync(person);
            co_await tx.CommitAsync();
        },
        appLoop);

    auto const fetched = SyncWaitPumping(dm.QuerySingleAsync<Person>(id), appLoop);
    CHECK(fetched.has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "Async.Transaction: rolled back work is discarded", "[Async][Transaction]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    DataMapper dm;
    dm.Connection().EnableAsync(dbWorkers, appLoop);
    dm.CreateTables<Person>();

    auto const id = SqlGuid::Create();
    RunPumped(
        [&]() -> Task<void> {
            AsyncSqlTransaction tx { dm.Connection() };
            co_await tx.BeginAsync();
            auto person = Person { .id = id, .name = "RolledBack", .age = 1 };
            co_await dm.CreateAsync(person);
            co_await tx.RollbackAsync();
        },
        appLoop);

    auto const fetched = SyncWaitPumping(dm.QuerySingleAsync<Person>(id), appLoop);
    CHECK_FALSE(fetched.has_value());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Async.Transaction: an open transaction commits by default on destruction",
                 "[Async][Transaction]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    DataMapper dm;
    dm.Connection().EnableAsync(dbWorkers, appLoop);
    dm.CreateTables<Person>();

    auto const id = SqlGuid::Create();
    RunPumped(
        [&]() -> Task<void> {
            AsyncSqlTransaction tx { dm.Connection() };
            co_await tx.BeginAsync(); // default mode is COMMIT, matching the synchronous SqlTransaction
            auto person = Person { .id = id, .name = "DefaultCommit", .age = 1 };
            co_await dm.CreateAsync(person);
            // No explicit Commit/Rollback: ~AsyncSqlTransaction finalizes through the strand using the
            // configured default mode (COMMIT), so the work must be persisted.
        },
        appLoop);

    auto const fetched = SyncWaitPumping(dm.QuerySingleAsync<Person>(id), appLoop);
    CHECK(fetched.has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "Async.Transaction: a second BeginAsync without finalizing throws", "[Async][Transaction]")
{
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;
    DataMapper dm;
    dm.Connection().EnableAsync(dbWorkers, appLoop);
    dm.CreateTables<Person>();

    bool threw = false;
    RunPumped(
        [&]() -> Task<void> {
            AsyncSqlTransaction tx { dm.Connection() };
            co_await tx.BeginAsync();
            try
            {
                co_await tx.BeginAsync(); // already open -> std::logic_error surfaced at the co_await
            }
            catch (std::logic_error const&)
            {
                threw = true;
            }
            co_await tx.RollbackAsync();
        },
        appLoop);

    CHECK(threw);
}
