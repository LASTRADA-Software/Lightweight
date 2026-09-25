// SPDX-License-Identifier: Apache-2.0

// The relation accessors report an unavailable relation through RelationResult (std::expected) rather
// than by throwing: why it is unavailable, whether that is remembered, and what still propagates.

#include "../Utils.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <optional>
#include <stdexcept>
#include <string>

using namespace Lightweight;

TEST_CASE("RelationResult: a hand-built record reports NotConfigured", "[DataMapper][RelationResult]")
{
    auto email = Email { .id = SqlGuid::Create(), .address = "a@example.com", .user = SqlGuid::Create() };
    auto user = User { .id = SqlGuid::Create(), .name = "Alice", .emails = {} };

    CHECK(email.user.Record().error() == RelationError::NotConfigured);
    CHECK(user.emails.All().error() == RelationError::NotConfigured);
    CHECK(user.emails.Count().error() == RelationError::NotConfigured);
    CHECK(user.emails.Each([](Email const&) {}).error() == RelationError::NotConfigured);
    CHECK_THROWS_AS(std::ignore = email.user->name, SqlRequireLoadedError);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RelationResult: a NULL foreign key reports NotFound until it is re-pointed",
                 "[DataMapper][RelationResult]")
{
    auto dm = DataMapper {};
    dm.CreateTables<User, NullableForeignKeyUser>();
    auto user = User { .id = SqlGuid::Create(), .name = "Alice", .emails = {} };
    dm.Create(user);
    auto withoutUser = NullableForeignKeyUser { .id = SqlGuid::Create(), .user = std::nullopt };
    dm.Create(withoutUser);

    auto loaded = dm.QuerySingle<NullableForeignKeyUser>(withoutUser.id.Value());
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
        return;

    CHECK(loaded->user.Record().error() == RelationError::NotFound);

    // Pointing the relation at a record forgets what was known about the old key.
    loaded->user = user;
    auto const pointed = loaded->user.Record();
    REQUIRE(pointed.has_value());
    if (pointed.has_value())
        CHECK(pointed->get().name.Value() == "Alice");
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RelationResult: a failed query is reported as QueryFailed and retried on the next access",
                 "[DataMapper][RelationResult]")
{
    auto dm = DataMapper {};
    dm.CreateTables<User, Email>();
    auto user = User { .id = SqlGuid::Create(), .name = "Alice", .emails = {} };
    dm.Create(user);
    auto email = Email { .id = SqlGuid::Create(), .address = "alice@example.com", .user = user };
    dm.Create(email);

    auto loaded = dm.QuerySingle<Email>(email.id.Value());
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
        return;

    auto stmt = SqlStatement { dm.Connection() };
    std::ignore = SqlTestFixture::DropTableRecursively(
        stmt, { .catalog = {}, .schema = {}, .table = std::string { RecordTableName<User> } });
    CHECK(loaded->user.Record().error() == RelationError::QueryFailed);

    // Not remembered: once the table is back, the very same relation loads.
    dm.CreateTables<User>();
    dm.CreateExplicit(user);
    auto const owner = loaded->user.Record();
    REQUIRE(owner.has_value());
    if (owner.has_value())
        CHECK(owner->get().name.Value() == "Alice");
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RelationResult: an exception thrown by an Each() callback propagates unchanged",
                 "[DataMapper][RelationResult]")
{
    auto dm = DataMapper {};
    dm.CreateTables<User, Email>();
    auto user = User { .id = SqlGuid::Create(), .name = "Alice", .emails = {} };
    dm.Create(user);
    auto email = Email { .id = SqlGuid::Create(), .address = "alice@example.com", .user = user };
    dm.Create(email);

    auto loaded = dm.QuerySingle<User>(user.id.Value());
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
        return;

    // The caller's own exception is not a failed load: it must not turn into QueryFailed.
    CHECK_THROWS_AS(std::ignore = loaded->emails.Each([](Email const&) { throw std::logic_error { "caller" }; }),
                    std::logic_error);
}

TEST_CASE("RelationResult: RelationError formats by name", "[DataMapper][RelationResult]")
{
    CHECK(std::format("{}", RelationError::Outdated) == "Outdated");
    CHECK(std::format("{}", RelationError::QueryFailed) == "QueryFailed");
    CHECK(SqlRequireLoadedError { "BelongsTo<X>", RelationError::NotFound }.Error() == RelationError::NotFound);
}
