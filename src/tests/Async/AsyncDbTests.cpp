// SPDX-License-Identifier: Apache-2.0

// clang-format off
#include "../Utils.hpp" // must precede Entities.hpp, which uses the Member() macro defined there
#include "../DataMapper/Entities.hpp"
// clang-format on

#include "AsyncTestUtils.hpp"

#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <ranges>
#include <stdexcept>

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

    // Single-record-by-primary-key uses the QuerySingleAsync shorthand.
    auto const fetched = SyncWaitPumping(dm.QuerySingleAsync<Person>(person.id.Value()), appLoop);
    CHECK(RequireValue(fetched).name == person.name);

    auto updated = RequireValue(fetched);
    updated.name = "Alicia";
    SyncWaitPumping(dm.UpdateAsync(updated), appLoop);

    auto const reFetched = SyncWaitPumping(dm.QuerySingleAsync<Person>(person.id.Value()), appLoop);
    CHECK(RequireValue(reFetched).name == updated.name);

    auto const deleted = SyncWaitPumping(dm.DeleteAsync(RequireValue(reFetched)), appLoop);
    CHECK(deleted == 1);

    auto const gone = SyncWaitPumping(dm.QuerySingleAsync<Person>(person.id.Value()), appLoop);
    CHECK_FALSE(gone.has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "Async.DataMapper: QueryAsync builder finishers", "[Async][DataMapper]")
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

    SECTION("All() returns every row and matches the synchronous builder")
    {
        auto const allAsync = SyncWaitPumping(dm.QueryAsync<Person>().All(), appLoop);
        auto const allSync = dm.Query<Person>().All();
        CHECK(allAsync.size() == 5);
        CHECK(allAsync.size() == allSync.size());
    }

    SECTION("Count() and Exist() report the filtered cardinality")
    {
        auto const count =
            SyncWaitPumping(dm.QueryAsync<Person>().Where(FieldNameOf<Member(Person::age)>, ">=", 22).Count(), appLoop);
        CHECK(count == 3); // ages 22, 23, 24

        auto const exists =
            SyncWaitPumping(dm.QueryAsync<Person>().Where(FieldNameOf<Member(Person::age)>, "=", 24).Exist(), appLoop);
        CHECK(exists);

        auto const missing =
            SyncWaitPumping(dm.QueryAsync<Person>().Where(FieldNameOf<Member(Person::age)>, "=", 99).Exist(), appLoop);
        CHECK_FALSE(missing);
    }

    SECTION("First(n) and Range() honor ordering and limits")
    {
        auto const firstTwo = SyncWaitPumping(
            dm.QueryAsync<Person>().OrderBy(FieldNameOf<Member(Person::age)>, SqlResultOrdering::ASCENDING).First(2),
            appLoop);
        REQUIRE(firstTwo.size() == 2);
        CHECK(firstTwo[0].age.Value() == 20);
        CHECK(firstTwo[1].age.Value() == 21);

        auto const ranged = SyncWaitPumping(
            dm.QueryAsync<Person>().OrderBy(FieldNameOf<Member(Person::age)>, SqlResultOrdering::ASCENDING).Range(1, 2),
            appLoop);
        REQUIRE(ranged.size() == 2);
        CHECK(ranged[0].age.Value() == 21);
        CHECK(ranged[1].age.Value() == 22);
    }

    SECTION("First<Field>() returns a single scalar")
    {
        auto const age = SyncWaitPumping(
            dm.QueryAsync<Person>().Where(FieldNameOf<Member(Person::age)>, "=", 23).First<Member(Person::age)>(), appLoop);
        CHECK(RequireValue(age) == 23);
    }

    SECTION("First() by primary key agrees with QuerySingleAsync")
    {
        auto known = Person { .id = SqlGuid::Create(), .name = "Zoe", .age = 77 };
        dm.Create(known);

        // The QuerySingleAsync shorthand and the builder's First() finisher must return the same record.
        auto const singleResult = SyncWaitPumping(dm.QuerySingleAsync<Person>(known.id.Value()), appLoop);
        auto const builderResult = SyncWaitPumping(
            dm.QueryAsync<Person>().Where(FieldNameOf<Member(Person::id)>, "=", known.id.Value()).First(), appLoop);
        auto const& viaSingle = RequireValue(singleResult);
        auto const& viaBuilder = RequireValue(builderResult);

        CHECK(viaSingle.name.Value() == "Zoe");
        CHECK(viaBuilder.name.Value() == viaSingle.name.Value());
        CHECK(viaBuilder.age.Value() == viaSingle.age.Value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "Async.DataMapper: async method without EnableAsync throws", "[Async][DataMapper]")
{
    DataMapper dm; // EnableAsync intentionally NOT called

    auto person = Person { .id = SqlGuid::Create(), .name = "X", .age = 1 };
    // The *Async methods are not coroutines: they evaluate AsyncBackend() eagerly at the call site,
    // so the precondition violation throws here (fail-fast) rather than as undefined behavior.
    CHECK_THROWS_AS((void) dm.CreateAsync(person), std::logic_error);

    // The async builder finishers likewise evaluate AsyncBackend() eagerly when the finisher is called.
    dm.CreateTables<Person>();
    CHECK_THROWS_AS((void) dm.QueryAsync<Person>().All(), std::logic_error);
}
