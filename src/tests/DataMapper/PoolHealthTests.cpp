// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"

#include <Lightweight/DataMapper/Pool.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <ranges>
#include <thread>

using namespace Lightweight;
using namespace std::chrono_literals;

namespace
{

/// A clock the test drives by hand, so the pool's time-based retirement is exercised deterministically
/// rather than by sleeping for the length of the configured bound.
///
/// Seeded from the real clock so that entries created before the pool's clock is overridden (the
/// pre-created ones, made in the constructor) still carry sensible timestamps.
class FakeClock
{
  public:
    [[nodiscard]] std::chrono::steady_clock::time_point Now() const noexcept
    {
        return _now;
    }

    void Advance(std::chrono::milliseconds delta) noexcept
    {
        _now += delta;
    }

  private:
    std::chrono::steady_clock::time_point _now { std::chrono::steady_clock::now() };
};

// A pool of exactly one connection, so the second acquirer is guaranteed to find it exhausted.
constexpr auto SingleSlotWaitConfig = PoolConfig {
    .initialSize = 0,
    .maxSize = 1,
    .growthStrategy = GrowthStrategy::BoundedWait,
};

} // namespace

// ================================================================================================
// Acquire timeout
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "Pool: Acquire with timeout reports Timeout on an exhausted pool", "[Pool]")
{
    auto pool = Pool<SingleSlotWaitConfig> {};
    auto const held = pool.Acquire(); // occupies the pool's only slot

    auto const start = std::chrono::steady_clock::now();
    auto second = pool.Acquire(50ms);
    auto const elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(second.has_value());
    CHECK(second.error() == PoolError::Timeout);
    // Slack against clock granularity and an early wake-up: the point is that it genuinely waited
    // rather than failing instantly, not that the wait was accurate to the millisecond.
    CHECK(elapsed >= 40ms);
    // The timed-out acquirer must de-register itself, or the pool would later hand a connection to a
    // waiter that no longer exists.
    CHECK(pool.WaiterCount() == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: a timed-out acquirer leaves no ghost waiter behind", "[Pool]")
{
    auto pool = Pool<SingleSlotWaitConfig> {};
    {
        auto const held = pool.Acquire();
        REQUIRE_FALSE(pool.Acquire(20ms).has_value());
        REQUIRE(pool.WaiterCount() == 0);
    } // held is returned here

    // With the abandoned waiter properly removed, the returned connection goes idle rather than being
    // handed off into a dead node (which would lose it, leaving the pool permanently short).
    CHECK(pool.IdleCount() == 1);
    CHECK(pool.Acquire(0ms).has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: Acquire with timeout succeeds once a connection is returned", "[Pool]")
{
    auto pool = Pool<SingleSlotWaitConfig> {};
    auto held = std::optional { pool.Acquire() };

    auto releaser = std::jthread { [&held] {
        std::this_thread::sleep_for(20ms);
        held.reset(); // returns the connection to the pool
    } };

    auto acquired = pool.Acquire(10s);
    releaser.join();

    CHECK(acquired.has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: Acquire with timeout never fails for the non-waiting strategies", "[Pool]")
{
    constexpr auto Config = PoolConfig {
        .initialSize = 0,
        .maxSize = 1,
        .growthStrategy = GrowthStrategy::BoundedOverflow,
    };
    auto pool = Pool<Config> {};

    auto const held = pool.Acquire();
    // Past maxSize, but this strategy creates rather than waits, so even a zero timeout succeeds.
    auto overflow = pool.Acquire(0ms);
    CHECK(overflow.has_value());
}

// ================================================================================================
// Validation on borrow
//
// A connection closed via SqlConnection::Close() reports IsAlive() == false, which is what the pool's
// borrow-time check keys on. Closing one before it goes idle therefore reproduces exactly what a
// firewall or a server restart leaves behind, without needing either.
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "Pool: a dead connection is replaced when validation is enabled", "[Pool]")
{
    constexpr auto Config = PoolConfig {
        .initialSize = 0,
        .maxSize = 4,
        .growthStrategy = GrowthStrategy::BoundedOverflow,
        .validateOnBorrow = ValidateOnBorrow::Yes,
    };
    auto pool = Pool<Config> {};

    {
        auto held = pool.Acquire();
        held->Connection().Close();
        REQUIRE_FALSE(held->Connection().IsAlive());
    } // the dead connection is idled here
    REQUIRE(pool.IdleCount() == 1);

    auto const acquired = pool.Acquire();
    CHECK(acquired->Connection().IsAlive());
    // The dead one was retired rather than parked back in the idle set.
    CHECK(pool.IdleCount() == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: a dead connection is handed out when validation is disabled", "[Pool]")
{
    constexpr auto Config = PoolConfig {
        .initialSize = 0,
        .maxSize = 4,
        .growthStrategy = GrowthStrategy::BoundedOverflow,
        .validateOnBorrow = ValidateOnBorrow::No,
    };
    auto pool = Pool<Config> {};

    {
        auto held = pool.Acquire();
        held->Connection().Close();
    }
    REQUIRE(pool.IdleCount() == 1);

    // Establishes the baseline the other tests rely on: without a check of some kind, the pool really
    // does hand the broken connection straight back to the next caller.
    auto const acquired = pool.Acquire();
    CHECK_FALSE(acquired->Connection().IsAlive());
}

// ================================================================================================
// Idle-time and lifetime bounds
//
// Validation is switched off in these tests so that retirement can only be attributed to the time
// bound under test: the pooled connection is deliberately dead, so a live connection coming back out
// proves the bound retired it and the pool built a replacement.
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "Pool: maxIdleTime retires a connection that sat idle too long", "[Pool]")
{
    constexpr auto Config = PoolConfig {
        .initialSize = 0,
        .maxSize = 4,
        .growthStrategy = GrowthStrategy::BoundedOverflow,
        .validateOnBorrow = ValidateOnBorrow::No,
        .maxIdleTimeMs = 1000,
    };
    auto pool = Pool<Config> {};
    auto clock = FakeClock {};
    pool.SetClock([&clock] { return clock.Now(); });

    {
        auto held = pool.Acquire();
        held->Connection().Close();
    }
    REQUIRE(pool.IdleCount() == 1);

    {
        // Still within the bound: the same (dead) connection comes back.
        clock.Advance(500ms);
        auto const acquired = pool.Acquire();
        CHECK_FALSE(acquired->Connection().IsAlive());
    }

    clock.Advance(1500ms);
    auto const acquired = pool.Acquire();
    CHECK(acquired->Connection().IsAlive()); // retired past the bound, replaced with a fresh connection
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: maxLifetime retires a connection on its way back to the pool", "[Pool]")
{
    constexpr auto Config = PoolConfig {
        .initialSize = 0,
        .maxSize = 4,
        .growthStrategy = GrowthStrategy::BoundedOverflow,
        .validateOnBorrow = ValidateOnBorrow::No,
        .maxLifetimeMs = 1000,
    };
    auto pool = Pool<Config> {};
    auto clock = FakeClock {};
    pool.SetClock([&clock] { return clock.Now(); });

    {
        auto held = pool.Acquire();
        clock.Advance(1500ms); // ages past the lifetime bound while checked out
    }

    // Retired on return rather than idled, so the connection is released as soon as it is unwanted
    // instead of lingering until the next acquire.
    CHECK(pool.IdleCount() == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: maxLifetime measures total age, not time since last use", "[Pool]")
{
    constexpr auto Config = PoolConfig {
        .initialSize = 0,
        .maxSize = 4,
        .growthStrategy = GrowthStrategy::BoundedOverflow,
        .validateOnBorrow = ValidateOnBorrow::No,
        .maxLifetimeMs = 1000,
    };
    auto pool = Pool<Config> {};
    auto clock = FakeClock {};
    pool.SetClock([&clock] { return clock.Now(); });

    // Churn the connection repeatedly, each time well inside the bound. Were the age reset on every
    // return, this connection would live forever.
    {
        auto held = pool.Acquire();
        held->Connection().Close();
    }
    for ([[maybe_unused]] auto const _: std::views::iota(0, 3))
    {
        clock.Advance(300ms);
        auto const held = pool.Acquire();
        CHECK_FALSE(held->Connection().IsAlive()); // same connection each time: still within its lifetime
    }

    clock.Advance(300ms); // cumulative age now exceeds 1000ms
    auto const acquired = pool.Acquire();
    CHECK(acquired->Connection().IsAlive());
}

TEST_CASE_METHOD(SqlTestFixture, "Pool: BoundedWait releases capacity when it retires a connection", "[Pool]")
{
    constexpr auto Config = PoolConfig {
        .initialSize = 0,
        .maxSize = 1,
        .growthStrategy = GrowthStrategy::BoundedWait,
        .validateOnBorrow = ValidateOnBorrow::No,
        .maxLifetimeMs = 1000,
    };
    auto pool = Pool<Config> {};
    auto clock = FakeClock {};
    pool.SetClock([&clock] { return clock.Now(); });

    {
        auto held = pool.Acquire();
        clock.Advance(1500ms);
    } // retired instead of idled

    REQUIRE(pool.IdleCount() == 0);
    // The retired connection must still have given its slot back, or this bounded pool would be
    // permanently exhausted with nothing to hand out.
    CHECK(pool.Acquire(0ms).has_value());
}
