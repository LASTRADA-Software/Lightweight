// SPDX-License-Identifier: Apache-2.0

// clang-format off
#include "../Utils.hpp" // must precede Entities.hpp, which uses the Member() macro defined there
#include "../DataMapper/Entities.hpp"
// clang-format on

#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <ranges>

using namespace Lightweight;
using namespace Lightweight::Async;

TEST_CASE_METHOD(SqlTestFixture, "Async.DataMapper: CRUD via coroutines", "[Async][DataMapper]")
{
    // One background DB worker + an app run-loop that this (test) thread pumps.
    ThreadPoolExecutor dbWorkers { 1 };
    ManualExecutor appLoop;

    DataMapper dm;
    dm.Connection().EnableAsync(dbWorkers, appLoop);
    dm.CreateTables<Person>();

    auto person = Person { .id = SqlGuid::Create(), .name = "Alice", .age = 30 };
    SyncWaitPumping(dm.CreateAsync(person), appLoop);

    auto fetched = SyncWaitPumping(dm.QuerySingleAsync<Person>(person.id.Value()), appLoop);
    REQUIRE(fetched.has_value());
    CHECK(fetched->name == person.name);

    fetched->name = "Alicia";
    SyncWaitPumping(dm.UpdateAsync(*fetched), appLoop);

    auto reFetched = SyncWaitPumping(dm.QuerySingleAsync<Person>(person.id.Value()), appLoop);
    REQUIRE(reFetched.has_value());
    CHECK(reFetched->name == fetched->name);

    auto const deleted = SyncWaitPumping(dm.DeleteAsync(*reFetched), appLoop);
    CHECK(deleted == 1);

    auto gone = SyncWaitPumping(dm.QuerySingleAsync<Person>(person.id.Value()), appLoop);
    CHECK_FALSE(gone.has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "Async.DataMapper: QueryAsync returns multiple rows", "[Async][DataMapper]")
{
    ThreadPoolExecutor dbWorkers { 2 };
    ManualExecutor appLoop;

    DataMapper dm;
    dm.Connection().EnableAsync(dbWorkers, appLoop);
    dm.CreateTables<Person>();

    for (auto const index: std::views::iota(0, 5))
    {
        auto person = Person { .id = SqlGuid::Create(), .name = "P", .age = 20 + index };
        dm.Create(person);
    }

    auto const all =
        SyncWaitPumping(dm.QueryAsync<Person>("SELECT \"id\", \"name\", \"is_active\", \"age\" FROM \"Person\""), appLoop);
    CHECK(all.size() == 5);
}
