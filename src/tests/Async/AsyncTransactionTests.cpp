// SPDX-License-Identifier: Apache-2.0

// clang-format off
#include "../Utils.hpp" // must precede Entities.hpp, which uses the Member() macro defined there
#include "../DataMapper/Entities.hpp"
// clang-format on

#include <Lightweight/Async/AsyncSqlTransaction.hpp>
#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/Task.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

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
    auto task = [&]() -> Task<void> {
        AsyncSqlTransaction tx { dm.Connection() };
        co_await tx.BeginAsync();
        auto person = Person { .id = id, .name = "Committed", .age = 1 };
        co_await dm.CreateAsync(person);
        co_await tx.CommitAsync();
    }();
    SyncWaitPumping(std::move(task), appLoop);

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
    auto task = [&]() -> Task<void> {
        AsyncSqlTransaction tx { dm.Connection() };
        co_await tx.BeginAsync();
        auto person = Person { .id = id, .name = "RolledBack", .age = 1 };
        co_await dm.CreateAsync(person);
        co_await tx.RollbackAsync();
    }();
    SyncWaitPumping(std::move(task), appLoop);

    auto const fetched = SyncWaitPumping(dm.QuerySingleAsync<Person>(id), appLoop);
    CHECK_FALSE(fetched.has_value());
}
