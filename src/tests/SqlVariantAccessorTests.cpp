// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/SqlDate.hpp>
#include <Lightweight/DataBinder/SqlDateTime.hpp>
#include <Lightweight/DataBinder/SqlGuid.hpp>
#include <Lightweight/DataBinder/SqlNullValue.hpp>
#include <Lightweight/DataBinder/SqlText.hpp>
#include <Lightweight/DataBinder/SqlTime.hpp>
#include <Lightweight/DataBinder/SqlVariant.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace Lightweight;

// ================================================================================================
// Is<T> / IsNull
// ================================================================================================

TEST_CASE("SqlVariant::IsNull / Is<T>", "[SqlVariant]")
{
    CHECK(SqlVariant { SqlNullValue }.IsNull());
    CHECK_FALSE(SqlVariant { 42 }.IsNull());

    SqlVariant const v { 42 };
    CHECK(v.Is<int>());
    CHECK_FALSE(v.Is<double>());

    CHECK(SqlVariant { std::string { "x" } }.Is<std::string>());
    CHECK(SqlVariant { SqlNullValue }.Is<SqlNullType>());
}

// ================================================================================================
// Get<T>
// ================================================================================================

TEST_CASE("SqlVariant::Get<T> retrieves the held value", "[SqlVariant]")
{
    SqlVariant v { 42 };
    CHECK(v.Get<int>() == 42);
}

TEST_CASE("SqlVariant::Get<std::optional<T>> wraps the held value or yields nullopt", "[SqlVariant]")
{
    SqlVariant v { 7 };
    auto const wrapped = v.Get<std::optional<int>>();
    REQUIRE(wrapped.has_value());
    if (wrapped.has_value())
        CHECK(*wrapped == 7);

    SqlVariant nullVariant { SqlNullValue };
    auto const fromNull = nullVariant.Get<std::optional<int>>();
    CHECK_FALSE(fromNull.has_value());
}

// ================================================================================================
// TryGetIntegral family
// ================================================================================================

TEST_CASE("SqlVariant::TryGetInt / TryGetBool / TryGetLongLong", "[SqlVariant]")
{
    SqlVariant const i { 42 };
    CHECK(i.TryGetInt().value_or(-1) == 42);
    CHECK(i.TryGetLongLong().value_or(-1) == 42);
    CHECK(i.TryGetBool().value_or(false));

    SqlVariant const b { true };
    CHECK(b.TryGetBool().value_or(false));
    CHECK(b.TryGetInt().value_or(-1) == 1);

    SqlVariant const nullVariant { SqlNullValue };
    CHECK_FALSE(nullVariant.TryGetInt().has_value());
    CHECK_FALSE(nullVariant.TryGetBool().has_value());
}

TEST_CASE("SqlVariant::TryGetInt on a non-integral returns nullopt (noexcept contract)", "[SqlVariant]")
{
    // The public TryGet* wrappers are noexcept — must not propagate bad_variant_access.
    SqlVariant const s { std::string { "not a number" } };
    CHECK_FALSE(s.TryGetInt().has_value());
    CHECK_FALSE(s.TryGetBool().has_value());
    CHECK_FALSE(s.TryGetLongLong().has_value());
}

// ================================================================================================
// ValueOr
// ================================================================================================

TEST_CASE("SqlVariant::ValueOr returns held value or default for integral types", "[SqlVariant]")
{
    SqlVariant const v { 5 };
    CHECK(v.ValueOr(99) == 5);

    SqlVariant const nullVariant { SqlNullValue };
    CHECK(nullVariant.ValueOr(99) == 99);
}

TEST_CASE("SqlVariant::ValueOr returns default for non-integral on NULL", "[SqlVariant]")
{
    SqlVariant const nullVariant { SqlNullValue };
    auto const fallback = std::string { "fallback" };
    CHECK(nullVariant.ValueOr(std::string { "fallback" }) == fallback);
}

// ================================================================================================
// String views
// ================================================================================================

TEST_CASE("SqlVariant::TryGetStringView for std::string and std::string_view", "[SqlVariant]")
{
    SqlVariant const a { std::string { "hello" } };
    auto const aView = a.TryGetStringView();
    REQUIRE(aView.has_value());
    if (aView.has_value())
        CHECK(*aView == "hello");

    SqlVariant const b { std::string_view { "world" } };
    auto const bView = b.TryGetStringView();
    REQUIRE(bView.has_value());
    if (bView.has_value())
        CHECK(*bView == "world");

    SqlVariant const t { SqlText { .value = "text" } };
    auto const tView = t.TryGetStringView();
    REQUIRE(tView.has_value());
    if (tView.has_value())
        CHECK(*tView == "text");

    SqlVariant const nullVariant { SqlNullValue };
    CHECK_FALSE(nullVariant.TryGetStringView().has_value());
}

TEST_CASE("SqlVariant::TryGetStringView returns nullopt for an unrelated alternative (noexcept contract)", "[SqlVariant]")
{
    SqlVariant const v { 42 };
    CHECK_FALSE(v.TryGetStringView().has_value());
    CHECK_FALSE(v.TryGetUtf16StringView().has_value());
}

TEST_CASE("SqlVariant::TryGetUtf16StringView for std::u16string and std::u16string_view", "[SqlVariant]")
{
    SqlVariant const a { std::u16string { u"abc" } };
    auto const aView = a.TryGetUtf16StringView();
    REQUIRE(aView.has_value());
    if (aView.has_value())
        CHECK(*aView == std::u16string_view { u"abc" });

    SqlVariant const b { std::u16string_view { u"xyz" } };
    auto const bView = b.TryGetUtf16StringView();
    REQUIRE(bView.has_value());
    if (bView.has_value())
        CHECK(*bView == std::u16string_view { u"xyz" });

    SqlVariant const nullVariant { SqlNullValue };
    CHECK_FALSE(nullVariant.TryGetUtf16StringView().has_value());
}

// ================================================================================================
// TryGetDate / TryGetTime / TryGetDateTime
// ================================================================================================

TEST_CASE("SqlVariant::TryGetDate from SqlDate", "[SqlVariant]")
{
    SqlDate const d { std::chrono::year { 2026 }, std::chrono::month { 5 }, std::chrono::day { 6 } };
    SqlVariant const v { d };
    auto const date = v.TryGetDate();
    REQUIRE(date.has_value());
    if (date.has_value())
        CHECK(*date == d);
}

TEST_CASE("SqlVariant::TryGetDate downcasts a SqlDateTime to a SqlDate", "[SqlVariant]")
{
    SqlDateTime const dt { std::chrono::year { 2026 },    std::chrono::month { 5 }, std::chrono::day { 6 }, 13h, 14min, 15s,
                           std::chrono::nanoseconds { 0 } };
    SqlVariant const v { dt };
    auto const date = v.TryGetDate();
    REQUIRE(date.has_value());
    if (date.has_value())
    {
        CHECK(date->sqlValue.year == 2026);
        CHECK(date->sqlValue.month == 5);
        CHECK(date->sqlValue.day == 6);
    }
}

TEST_CASE("SqlVariant::TryGetDate returns nullopt on NULL", "[SqlVariant]")
{
    SqlVariant const nullVariant { SqlNullValue };
    CHECK_FALSE(nullVariant.TryGetDate().has_value());
}

TEST_CASE("SqlVariant::TryGetDate throws for a non-date alternative", "[SqlVariant]")
{
    SqlVariant const v { 42 };
    CHECK_THROWS_AS((void) v.TryGetDate(), std::bad_variant_access);
}

TEST_CASE("SqlVariant::TryGetTime from SqlTime", "[SqlVariant]")
{
    SqlTime const t { 13h, 14min, 15s };
    SqlVariant const v { t };
    auto const time = v.TryGetTime();
    REQUIRE(time.has_value());
    if (time.has_value())
        CHECK(time->sqlValue.hour == 13);
}

TEST_CASE("SqlVariant::TryGetTime downcasts a SqlDateTime to a SqlTime", "[SqlVariant]")
{
    SqlDateTime const dt { std::chrono::year { 2026 },    std::chrono::month { 5 }, std::chrono::day { 6 }, 13h, 14min, 15s,
                           std::chrono::nanoseconds { 0 } };
    SqlVariant const v { dt };
    auto const time = v.TryGetTime();
    REQUIRE(time.has_value());
    if (time.has_value())
    {
        CHECK(time->sqlValue.hour == 13);
        CHECK(time->sqlValue.minute == 14);
        CHECK(time->sqlValue.second == 15);
    }
}

TEST_CASE("SqlVariant::TryGetDateTime from SqlDateTime", "[SqlVariant]")
{
    SqlDateTime const dt { std::chrono::year { 2026 },    std::chrono::month { 5 }, std::chrono::day { 6 }, 13h, 14min, 15s,
                           std::chrono::nanoseconds { 0 } };
    SqlVariant const v { dt };
    auto const fetched = v.TryGetDateTime();
    REQUIRE(fetched.has_value());
    if (fetched.has_value())
        CHECK(fetched->sqlValue.year == 2026);
}

TEST_CASE("SqlVariant::TryGetDateTime throws for a non-datetime alternative", "[SqlVariant]")
{
    SqlVariant const v { 42 };
    CHECK_THROWS_AS((void) v.TryGetDateTime(), std::bad_variant_access);
}

TEST_CASE("SqlVariant::TryGetDateTime returns nullopt on NULL", "[SqlVariant]")
{
    SqlVariant const nullVariant { SqlNullValue };
    CHECK_FALSE(nullVariant.TryGetDateTime().has_value());
}

// ================================================================================================
// TryGetGuid
// ================================================================================================

TEST_CASE("SqlVariant::TryGetGuid from SqlGuid", "[SqlVariant]")
{
    auto const g = SqlGuid::Create();
    SqlVariant const v { g };
    auto const guid = v.TryGetGuid();
    REQUIRE(guid.has_value());
    if (guid.has_value())
        CHECK(*guid == g);
}

TEST_CASE("SqlVariant::TryGetGuid throws for a non-GUID alternative", "[SqlVariant]")
{
    SqlVariant const v { 42 };
    CHECK_THROWS_AS((void) v.TryGetGuid(), std::bad_variant_access);
}

TEST_CASE("SqlVariant::TryGetGuid returns nullopt on NULL", "[SqlVariant]")
{
    SqlVariant const nullVariant { SqlNullValue };
    CHECK_FALSE(nullVariant.TryGetGuid().has_value());
}

// ================================================================================================
// Equality / inequality based on ToString
// ================================================================================================

TEST_CASE("SqlVariant::operator== compares the string representation", "[SqlVariant]")
{
    SqlVariant const a { 42 };
    SqlVariant const b { 42 };
    SqlVariant const c { 43 };
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("SqlVariant: NULL equals NULL via ToString", "[SqlVariant]")
{
    SqlVariant const a { SqlNullValue };
    SqlVariant const b { SqlNullValue };
    CHECK(a == b);
    CHECK(a.ToString() == "NULL");
}
