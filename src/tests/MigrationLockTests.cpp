// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/SqlScopedLock.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>
#include <utility>

using namespace Lightweight;

TEST_CASE_METHOD(SqlTestFixture, "SqlScopedLock throws when the lock is already held", "[SqlScopedLock]")
{
    // Two distinct sessions are required: SQL Server's `sp_getapplock` (with
    // `@LockOwner=Session`) and PostgreSQL's `pg_advisory_lock` are both
    // reentrant on the same connection, so acquiring twice through one session
    // succeeds. Cross-session contention is the path that throws on every
    // backend (including SQLite, whose lock table just rejects the duplicate).
    auto firstConn = SqlConnection {};
    auto secondConn = SqlConnection {};

    auto first = SqlScopedLock { firstConn, "duplicate_lock", std::chrono::milliseconds { 50 } };
    REQUIRE(first.IsLocked());

    auto const _ = ScopedSqlNullLogger {}; // suppress the expected error noise

    CHECK_THROWS_AS((SqlScopedLock { secondConn, "duplicate_lock", std::chrono::milliseconds { 50 } }), std::runtime_error);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlScopedLock is move-constructible and transfers ownership", "[SqlScopedLock]")
{
    auto stmt = SqlStatement {};

    auto first = SqlScopedLock { stmt.Connection(), "movable_lock" };
    REQUIRE(first.IsLocked());

    auto second = SqlScopedLock { std::move(first) };
    CHECK(second.IsLocked());
    // The moved-from `first`'s destructor running cleanly at scope end exercises
    // the invariant that it no longer owns the lock (a double-release would error).
}

TEST_CASE_METHOD(SqlTestFixture, "SqlScopedLock move assignment releases prior lock and adopts new one", "[SqlScopedLock]")
{
    auto stmt = SqlStatement {};

    auto first = SqlScopedLock { stmt.Connection(), "lock_A" };
    auto second = SqlScopedLock { stmt.Connection(), "lock_B" };

    REQUIRE(first.IsLocked());
    REQUIRE(second.IsLocked());

    first = std::move(second); // releases lock_A, adopts lock_B
    CHECK(first.IsLocked());

    // lock_A must now be re-acquirable since first's prior lock was released.
    // The moved-from `second`'s destructor running cleanly at scope end also
    // exercises the invariant that it no longer owns lock_B (first does).
    auto reacquireA = SqlScopedLock { stmt.Connection(), "lock_A" };
    CHECK(reacquireA.IsLocked());
}
