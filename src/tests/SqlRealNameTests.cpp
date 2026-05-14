// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlRealName.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <type_traits>

using Lightweight::SqlRealName;

TEST_CASE("SqlRealName: literal construction stores the trailing NUL byte", "[SqlRealName]")
{
    constexpr SqlRealName name { "Users" };

    // The template parameter N is the array size including the NUL, so length == N - 1.
    STATIC_REQUIRE(name.size() == 5);
    STATIC_REQUIRE(!name.empty());
    CHECK(name.sv() == std::string_view { "Users" });
}

TEST_CASE("SqlRealName: empty literal reports zero length and empty()", "[SqlRealName]")
{
    constexpr SqlRealName name { "" };

    STATIC_REQUIRE(name.empty());
    STATIC_REQUIRE(decltype(name)::length == 0); // exercise the public static constant too
    CHECK(name.sv().empty());
}

TEST_CASE("SqlRealName: begin / end span the value characters without the NUL", "[SqlRealName]")
{
    constexpr SqlRealName name { "abc" };
    REQUIRE(name.end() - name.begin() == 3);
    CHECK(std::string(name.begin(), name.end()) == "abc");
}

TEST_CASE("SqlRealName: implicit conversion to std::string_view", "[SqlRealName]")
{
    constexpr SqlRealName name { "Hello" };

    // Implicit-conversion site — exercise both initialization forms.
    std::string_view const view = name;
    CHECK(view == "Hello");

    auto consume = [](std::string_view sv) {
        return std::string(sv);
    };
    CHECK(consume(name) == "Hello");
}

TEST_CASE("SqlRealName: three-way comparison matches lexicographic order on the buffer", "[SqlRealName]")
{
    constexpr SqlRealName a { "Alpha" };
    constexpr SqlRealName b { "Alpha" };
    constexpr SqlRealName c { "Bravo" };

    STATIC_REQUIRE(a == b);
    STATIC_REQUIRE(a < c);
    STATIC_REQUIRE(c > a);
    STATIC_REQUIRE(a != c);
}

TEST_CASE("SqlRealName: usable as a non-type template parameter", "[SqlRealName]")
{
    auto extract = []<SqlRealName N>() -> std::string_view {
        return N.sv();
    };

    CHECK(extract.template operator()<SqlRealName { "MyTable" }>() == "MyTable");
    CHECK(extract.template operator()<SqlRealName { "Other" }>() == "Other");
}

TEST_CASE("SqlRealName: copy and move are noexcept and structurally identical", "[SqlRealName]")
{
    STATIC_REQUIRE(std::is_nothrow_copy_constructible_v<SqlRealName<6>>);
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<SqlRealName<6>>);
    STATIC_REQUIRE(std::is_nothrow_copy_assignable_v<SqlRealName<6>>);
    STATIC_REQUIRE(std::is_nothrow_move_assignable_v<SqlRealName<6>>);

    constexpr SqlRealName original { "Posts" };
    SqlRealName copy { original };
    CHECK(copy.sv() == "Posts");

    SqlRealName<6> assigned {};
    assigned = original;
    CHECK(assigned.sv() == "Posts");
}
