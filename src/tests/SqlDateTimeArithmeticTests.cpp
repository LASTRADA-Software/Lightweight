// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/SqlDateTime.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace Lightweight;

// ================================================================================================
// SqlDateTime in-place arithmetic operators
// ================================================================================================

namespace
{

constexpr auto kBase =
    SqlDateTime { std::chrono::year { 2026 },    std::chrono::month { 5 }, std::chrono::day { 6 }, 12h, 0min, 0s,
                  std::chrono::nanoseconds { 0 } };

} // namespace

TEST_CASE("SqlDateTime::operator+= adds a duration in place", "[SqlDateTime]")
{
    auto value = kBase;
    value += 30min;
    CHECK(value == kBase + 30min);

    value += 24h;
    CHECK(value == kBase + 30min + 24h);
}

TEST_CASE("SqlDateTime::operator-= subtracts a duration in place", "[SqlDateTime]")
{
    auto value = kBase;
    value -= 1h;
    CHECK(value == kBase - 1h);

    value -= 24h;
    CHECK(value == kBase - 1h - 24h);
}

TEST_CASE("SqlDateTime: friend operator+ leaves the original unchanged", "[SqlDateTime]")
{
    auto const result = kBase + 1h;
    CHECK(result.sqlValue.hour == 13);
    // Original is untouched.
    CHECK(kBase.sqlValue.hour == 12);
}

TEST_CASE("SqlDateTime: equality compares the underlying time point", "[SqlDateTime]")
{
    auto const a = kBase;
    auto const b = kBase + 0s;
    CHECK(a == b);
}

TEST_CASE("SqlDateTime::value() round-trips through the constructor", "[SqlDateTime]")
{
    auto const native = kBase.value();
    SqlDateTime const reconstructed { native };
    CHECK(reconstructed == kBase);
}
