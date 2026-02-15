// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// NOLINTBEGIN(*)

//  Due to a name clash with rpcndr.h
//  C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared\rpcndr.h(201,15): note: expanded from macro 'small'
//  201 | #define small char
//
//  We undefine it here
#undef small

#include <future>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

using namespace std::string_view_literals;
using namespace std::string_literals;
using namespace Lightweight;

TEST_CASE("Fetch from Local data mapper", "[DataMapper],[Executors]")
{
    auto pool = exec::static_thread_pool(3);

    DataMapper& dm = DataMapper::AcquireThreadLocal();
    dm.CreateTables<User, Email>();

    auto user = User { .id = SqlGuid::Create(), .name = "John Doe" };
    dm.Create(user);

    std::vector<Email> emails;
    for (auto const i: std::views::iota(0, 3))
    {
        emails.emplace_back(Email { .id = SqlGuid::Create(), .address = std::format("john{}@doe.com", i), .user = user });
        dm.Create(emails.back());
    }

    // Get a handle to the thread pool:
    auto sched = pool.get_scheduler();

    auto fetch = [](Light::SqlGuid id) -> Email {
        DataMapper& dm = DataMapper::AcquireThreadLocal();
        return dm.QuerySingle<Email>(id).value();
    };

    auto work = stdexec::when_all(stdexec::starts_on(sched, stdexec::just(emails[0].id.Value()) | stdexec::then(fetch)),
                                  stdexec::starts_on(sched, stdexec::just(emails[1].id.Value()) | stdexec::then(fetch)),
                                  stdexec::starts_on(sched, stdexec::just(emails[2].id.Value()) | stdexec::then(fetch)));

    // Launch the work and wait for the result
    auto [i, j, k] = stdexec::sync_wait(std::move(work)).value();

    CHECK(i.id == emails[0].id);
    CHECK(i.user->id == user.id);
    CHECK(i.user->name == user.name);

    CHECK(j.id == emails[1].id);
    CHECK(j.user->id == user.id);
    CHECK(j.user->name == user.name);

    CHECK(k.id == emails[2].id);
    CHECK(k.user->id == user.id);
    CHECK(k.user->name == user.name);
}

// {{{ Pool tests

TEST_CASE_METHOD(SqlTestFixture, "Pool: UnboundedGrow acquire and return", "[DataMapper],[Pool]")
{
    constexpr auto config = PoolConfig { .initialSize = 2, .maxSize = 0, .growthStrategy = GrowthStrategy::UnboundedGrow };
    auto pool = Pool<config>(config);

    // Pool should pre-create initialSize mappers
    CHECK(pool.IdleCount() == 2);

    {
        auto dm = pool.Acquire();
        CHECK(pool.IdleCount() == 1);

        // Acquired mapper is usable
        dm->CreateTables<Person>();
        auto person = Person { .id = SqlGuid::Create(), .name = "Alice" };
        dm->Create(person);
        auto fetched = dm->QuerySingle<Person>(person.id.Value());
        REQUIRE(fetched.has_value());
        CHECK(fetched->name == person.name);
    }

    // After PooledDataMapper destruction, mapper is returned to pool
    CHECK(pool.IdleCount() == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: UnboundedGrow grows beyond initialSize", "[DataMapper],[Pool]")
{
    constexpr auto config = PoolConfig { .initialSize = 1, .maxSize = 0, .growthStrategy = GrowthStrategy::UnboundedGrow };
    auto pool = Pool<config>(config);

    CHECK(pool.IdleCount() == 1);

    // Acquire more than initialSize
    auto dm1 = pool.Acquire();
    CHECK(pool.IdleCount() == 0);

    auto dm2 = pool.Acquire(); // grows beyond initialSize
    CHECK(pool.IdleCount() == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: BoundedOverflow drops excess on return", "[DataMapper],[Pool]")
{
    constexpr auto config = PoolConfig { .initialSize = 1, .maxSize = 2, .growthStrategy = GrowthStrategy::BoundedOverflow };
    auto pool = Pool<config>(config);

    CHECK(pool.IdleCount() == 1);

    // Acquire all pre-created + create overflow mappers
    auto dm1 = pool.Acquire();
    auto dm2 = pool.Acquire();
    auto dm3 = pool.Acquire();
    CHECK(pool.IdleCount() == 0);

    // Return dm3 — pool has room (0 < maxSize=2), so it's kept
    {
        auto temp = std::move(dm3);
    }
    CHECK(pool.IdleCount() == 1);

    // Return dm2 — pool has room (1 < maxSize=2), so it's kept
    {
        auto temp = std::move(dm2);
    }
    CHECK(pool.IdleCount() == 2);

    // Return dm1 — pool is full (2 >= maxSize=2), so it's destroyed
    {
        auto temp = std::move(dm1);
    }
    CHECK(pool.IdleCount() == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: BoundedWait blocks until mapper available", "[DataMapper],[Pool]")
{
    constexpr auto config = PoolConfig { .initialSize = 1, .maxSize = 1, .growthStrategy = GrowthStrategy::BoundedWait };
    auto pool = Pool<config>(config);

    CHECK(pool.IdleCount() == 1);

    std::atomic<bool> acquired = false;

    auto dm1 = pool.Acquire();
    CHECK(pool.IdleCount() == 0);

    // Launch a thread that tries to acquire — it should block
    auto future = std::async(std::launch::async, [&pool, &acquired] {
        auto dm = pool.Acquire();
        acquired = true;
    });

    // Give the thread time to start and block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(acquired);

    // Release dm1 — the blocked thread should now succeed
    {
        auto temp = std::move(dm1);
    }

    future.wait();
    CHECK(acquired);
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: Concurrent acquire with thread pool", "[DataMapper],[Pool],[Executors]")
{
    constexpr auto config = PoolConfig { .initialSize = 3, .maxSize = 0, .growthStrategy = GrowthStrategy::UnboundedGrow };
    auto pool = Pool<config>(config);

    DataMapper& dm = DataMapper::AcquireThreadLocal();
    dm.CreateTables<Person>();

    // Insert test data
    std::vector<Person> persons;
    for (int i = 0; i < 3; ++i)
    {
        persons.emplace_back(Person { .id = SqlGuid::Create(), .name = SqlAnsiString<25>(std::format("Person{}", i)) });
        dm.Create(persons.back());
    }

    auto threadPool = exec::static_thread_pool(3);
    auto sched = threadPool.get_scheduler();

    auto fetch = [&pool](Light::SqlGuid id) -> Person {
        auto dm = pool.Acquire();
        return dm->QuerySingle<Person>(id).value();
    };

    auto work = stdexec::when_all(stdexec::starts_on(sched, stdexec::just(persons[0].id.Value()) | stdexec::then(fetch)),
                                  stdexec::starts_on(sched, stdexec::just(persons[1].id.Value()) | stdexec::then(fetch)),
                                  stdexec::starts_on(sched, stdexec::just(persons[2].id.Value()) | stdexec::then(fetch)));

    auto [p0, p1, p2] = stdexec::sync_wait(std::move(work)).value();

    CHECK(p0.id == persons[0].id);
    CHECK(p0.name == persons[0].name);
    CHECK(p1.id == persons[1].id);
    CHECK(p1.name == persons[1].name);
    CHECK(p2.id == persons[2].id);
    CHECK(p2.name == persons[2].name);

    // All mappers should be returned to pool
    CHECK(pool.IdleCount() == 3);
}

// }}}

// NOLINTEND(*)
