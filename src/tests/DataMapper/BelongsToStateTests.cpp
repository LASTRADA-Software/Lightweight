// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <compare>
#include <stdexcept>
#include <string_view>
#include <utility>

using namespace Lightweight;

namespace
{
// Local helper: parse and `REQUIRE` the optional has a value, returning it.
// The explicit `if`-with-throw wrapper is what clang-tidy's
// `bugprone-unchecked-optional-access` analysis recognizes as a check —
// Catch2's `REQUIRE` is a macro it cannot reason about.
SqlGuid RequireParsed(std::string_view text)
{
    auto const parsed = SqlGuid::TryParse(text);
    REQUIRE(parsed.has_value());
    if (!parsed.has_value())
        throw std::runtime_error("REQUIRE failed but flow continued"); // unreachable
    return *parsed;
}
} // namespace

// ================================================================================================
// BelongsTo: trait checks
// ================================================================================================

TEST_CASE("BelongsTo traits: IsMandatory / IsOptional / IsPrimaryKey", "[BelongsTo]")
{
    using EmailUser = decltype(Email::user);
    STATIC_CHECK(EmailUser::IsMandatory);
    STATIC_CHECK_FALSE(EmailUser::IsOptional);
    STATIC_CHECK_FALSE(EmailUser::IsPrimaryKey);
    STATIC_CHECK_FALSE(EmailUser::IsAutoIncrementPrimaryKey);

    using NullableUser = decltype(NullableForeignKeyUser::user);
    STATIC_CHECK_FALSE(NullableUser::IsMandatory);
    STATIC_CHECK(NullableUser::IsOptional);
}

// ================================================================================================
// BelongsTo: explicit-value construction sets the foreign key
// ================================================================================================

TEST_CASE("BelongsTo<&User::id>: default-constructed is empty", "[BelongsTo]")
{
    Email email;
    CHECK_FALSE(static_cast<bool>(email.user));
    CHECK(!email.user);
}

TEST_CASE("BelongsTo<&User::id>: construction from a SqlGuid", "[BelongsTo]")
{
    auto const guid = SqlGuid::Create();
    Email email;
    email.user = guid;

    CHECK(static_cast<bool>(email.user));
    CHECK(email.user.Value() == guid);
}

// ================================================================================================
// BelongsTo: SetModified / IsModified
// ================================================================================================

TEST_CASE("BelongsTo<&User::id>: SetModified flips the dirty flag", "[BelongsTo]")
{
    Email email;
    email.user.SetModified(false);
    CHECK_FALSE(email.user.IsModified());

    email.user.SetModified(true);
    CHECK(email.user.IsModified());
}

TEST_CASE("BelongsTo<&User::id>: assigning a referenced-record marks it modified", "[BelongsTo]")
{
    User const u { .id = SqlGuid::Create(), .name = SqlAnsiString<30> { "Alice" } };
    Email email;
    email.user.SetModified(false);

    User mutableU = u;
    email.user = mutableU;
    CHECK(email.user.IsModified());
    CHECK(email.user.Value() == u.id.Value());
}

// ================================================================================================
// BelongsTo: assignment from SqlNullValue clears the relation
// ================================================================================================

TEST_CASE("BelongsTo<&User::id>: assignment from SqlNullValue clears the foreign key", "[BelongsTo]")
{
    Email email;
    email.user = SqlGuid::Create();
    REQUIRE(static_cast<bool>(email.user));

    email.user = SqlNullValue;
    CHECK_FALSE(static_cast<bool>(email.user));
    CHECK(!email.user);
    CHECK(email.user.IsModified());
}

// ================================================================================================
// BelongsTo: equality and ordering
// ================================================================================================

TEST_CASE("BelongsTo<&User::id>: equality and ordering against another BelongsTo", "[BelongsTo]")
{
    auto const guid1 = RequireParsed("00000000-0000-1000-8000-000000000001");
    auto const guid2 = RequireParsed("00000000-0000-1000-8000-000000000002");

    Email a;
    Email b;
    Email c;
    a.user = guid1;
    b.user = guid1;
    c.user = guid2;

    CHECK(a.user == b.user);
    CHECK(a.user != c.user);
    CHECK((a.user <=> b.user) == std::weak_ordering::equivalent);
    CHECK((a.user <=> c.user) == std::weak_ordering::less);
}

TEST_CASE("BelongsTo<&User::id>: equality against the underlying Field", "[BelongsTo]")
{
    auto const guid = SqlGuid::Create();
    User u { .id = guid, .name = SqlAnsiString<30> { "X" } };

    Email email;
    email.user = guid;

    CHECK(email.user == u.id);
    CHECK_FALSE(email.user != u.id);
}

// ================================================================================================
// BelongsTo: EmplaceRecord populates the cached referenced record
// ================================================================================================

TEST_CASE("BelongsTo<&User::id>::EmplaceRecord constructs a fresh referenced record", "[BelongsTo]")
{
    Email email;
    auto& user = email.user.EmplaceRecord();
    user.name = SqlAnsiString<30> { "Bob" };

    REQUIRE_NOTHROW(email.user.Record());
    CHECK(email.user.Record().name.Value() == "Bob");
}

// ================================================================================================
// BelongsTo: copy and move semantics preserve the foreign key
// ================================================================================================

TEST_CASE("BelongsTo<&User::id>: copy preserves the foreign key", "[BelongsTo]")
{
    auto const guid = SqlGuid::Create();
    Email original;
    original.user = guid;

    Email const copy = original;
    CHECK(copy.user.Value() == guid);
    CHECK(copy.user == original.user);
}

TEST_CASE("BelongsTo<&User::id>: move preserves the foreign key", "[BelongsTo]")
{
    auto const guid = SqlGuid::Create();
    auto source = decltype(Email::user) { guid };
    auto const moved = std::move(source);
    CHECK(moved.Value() == guid);
}

// ================================================================================================
// BelongsTo: operator! / operator bool agree with the empty / non-empty SqlGuid
// ================================================================================================

TEST_CASE("BelongsTo<&User::id>: operator bool reflects emptiness", "[BelongsTo]")
{
    Email email;
    CHECK(!email.user);
    CHECK_FALSE(static_cast<bool>(email.user));

    email.user = SqlGuid::Create();
    CHECK_FALSE(!email.user);
    CHECK(static_cast<bool>(email.user));
}

// ================================================================================================
// BelongsTo: ColumnNameOverride is exposed
// ================================================================================================

TEST_CASE("BelongsTo<&User::id, SqlRealName{...}>: ColumnNameOverride is exposed", "[BelongsTo]")
{
    using EmailUser = decltype(Email::user);
    STATIC_CHECK(EmailUser::ColumnNameOverride == std::string_view { "user_id" });
}
