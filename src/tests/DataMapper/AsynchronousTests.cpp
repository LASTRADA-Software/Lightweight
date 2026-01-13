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

// NOLINTEND(*)
