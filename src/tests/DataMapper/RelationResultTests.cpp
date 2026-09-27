// SPDX-License-Identifier: Apache-2.0

// The relation accessors report an unavailable relation through RelationResult (std::expected) rather
// than by throwing: why it is unavailable, whether that is remembered, and what still propagates.

#include "../Utils.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace Lightweight;

struct RrSupplier;
struct RrAccount;
struct RrRating;

struct RrSupplier
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};
    HasOneThrough<RrRating, Through<RrAccount>> rating {};
};

struct RrAccount
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    BelongsTo<Member(RrSupplier::id)> supplier {};
};

struct RrRating
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<int> score {};
    BelongsTo<Member(RrAccount::id)> account {};
};

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

TEST_CASE("RelationResult: a hand-built through relation reports NotConfigured", "[DataMapper][RelationResult]")
{
    auto physician = Physician { .id = SqlGuid::Create(), .name = "Dr. X", .appointments = {}, .patients = {} };
    auto supplier = RrSupplier { .id = {}, .name = "Supplier", .rating = {} };

    CHECK(physician.patients.All().error() == RelationError::NotConfigured);
    CHECK(std::as_const(physician).patients.All().error() == RelationError::NotConfigured);
    CHECK(physician.patients.Count().error() == RelationError::NotConfigured);
    CHECK(physician.patients.Each([](Patient const&) {}).error() == RelationError::NotConfigured);
    CHECK(supplier.rating.Record().error() == RelationError::NotConfigured);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RelationResult: an outdated relation is reported, and remembered, by every accessor",
                 "[DataMapper][RelationResult]")
{
    auto dm = DataMapper {};
    dm.CreateTables<User, Email, Physician, Patient, Appointment, RrSupplier, RrAccount, RrRating>();
    auto user = User { .id = SqlGuid::Create(), .name = "Alice", .emails = {} };
    dm.Create(user);
    auto physician = Physician { .id = SqlGuid::Create(), .name = "Dr. X", .appointments = {}, .patients = {} };
    dm.Create(physician);
    auto supplier = RrSupplier { .id = {}, .name = "Supplier", .rating = {} };
    dm.Create(supplier);

    auto loadedUser = dm.QuerySingle<User>(user.id.Value());
    auto loadedPhysician = dm.QuerySingle<Physician>(physician.id.Value());
    auto loadedSupplier = dm.QuerySingle<RrSupplier>(supplier.id.Value());
    REQUIRE(loadedUser.has_value());
    REQUIRE(loadedPhysician.has_value());
    REQUIRE(loadedSupplier.has_value());
    if (!loadedUser.has_value() || !loadedPhysician.has_value() || !loadedSupplier.has_value())
        return;

    auto const previous = SqlConnectionString { .value = SqlConnection::DefaultConnectionString().value };
    auto const restore = detail::Finally([&] { SqlConnection::SetDefaultConnectionString(previous); });
    SqlConnection::SetDefaultConnectionString(SqlConnectionString { previous.value + ";" });

    // Each accessor is asked twice: the first answer comes from the loader, the second from what the
    // relation remembered of it.
    SECTION("HasMany::All")
    {
        CHECK(loadedUser->emails.All().error() == RelationError::Outdated);
        CHECK(loadedUser->emails.All().error() == RelationError::Outdated);
    }
    SECTION("HasMany::Count")
    {
        CHECK(loadedUser->emails.Count().error() == RelationError::Outdated);
        CHECK(loadedUser->emails.Count().error() == RelationError::Outdated);
    }
    SECTION("HasMany::Each")
    {
        CHECK(loadedUser->emails.Each([](Email const&) {}).error() == RelationError::Outdated);
        CHECK(loadedUser->emails.Each([](Email const&) {}).error() == RelationError::Outdated);
    }
    SECTION("HasManyThrough::All")
    {
        CHECK(loadedPhysician->patients.All().error() == RelationError::Outdated);
        CHECK(loadedPhysician->patients.All().error() == RelationError::Outdated);
    }
    SECTION("HasManyThrough::Count")
    {
        CHECK(loadedPhysician->patients.Count().error() == RelationError::Outdated);
        CHECK(loadedPhysician->patients.Count().error() == RelationError::Outdated);
    }
    SECTION("HasManyThrough::Each")
    {
        CHECK(loadedPhysician->patients.Each([](Patient const&) {}).error() == RelationError::Outdated);
        CHECK(loadedPhysician->patients.Each([](Patient const&) {}).error() == RelationError::Outdated);
    }
    SECTION("HasOneThrough::Record")
    {
        CHECK(loadedSupplier->rating.Record().error() == RelationError::Outdated);
        CHECK(loadedSupplier->rating.Record().error() == RelationError::Outdated);
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RelationResult: Each walks records that are already loaded",
                 "[DataMapper][RelationResult]")
{
    auto dm = DataMapper {};
    dm.CreateTables<User, Email, Physician, Patient, Appointment>();
    auto user = User { .id = SqlGuid::Create(), .name = "Alice", .emails = {} };
    dm.Create(user);
    auto physician = Physician { .id = SqlGuid::Create(), .name = "Dr. X", .appointments = {}, .patients = {} };
    dm.Create(physician);
    for (auto const name: { std::string_view { "a" }, std::string_view { "b" } })
    {
        auto email = Email { .id = SqlGuid::Create(), .address = SqlAnsiString<30> { name }, .user = user };
        dm.Create(email);
        auto patient = Patient {
            .id = SqlGuid::Create(), .name = SqlAnsiString<30> { name }, .comment = "", .appointments = {}, .physicians = {}
        };
        dm.Create(patient);
        auto appointment = Appointment {
            .id = SqlGuid::Create(), .date = SqlDateTime::Now(), .comment = "", .physician = physician, .patient = patient
        };
        dm.Create(appointment);
    }

    auto loadedUser = dm.QuerySingle<User>(user.id.Value());
    auto loadedPhysician = dm.QuerySingle<Physician>(physician.id.Value());
    REQUIRE(loadedUser.has_value());
    REQUIRE(loadedPhysician.has_value());
    if (!loadedUser.has_value() || !loadedPhysician.has_value())
        return;

    // Loaded first, so Each() iterates the records in memory instead of streaming a query.
    REQUIRE(loadedUser->emails.All().has_value());
    REQUIRE(loadedPhysician->patients.All().has_value());

    auto emails = 0;
    CHECK(loadedUser->emails.Each([&](Email const&) { ++emails; }).has_value());
    CHECK(emails == 2);

    auto patients = 0;
    CHECK(loadedPhysician->patients.Each([&](Patient const&) { ++patients; }).has_value());
    CHECK(patients == 2);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RelationResult: a referenced row that no longer exists reports NotFound",
                 "[DataMapper][RelationResult]")
{
    auto dm = DataMapper {};
    dm.CreateTables<User, Email, RrSupplier, RrAccount, RrRating>();
    auto user = User { .id = SqlGuid::Create(), .name = "Alice", .emails = {} };
    dm.Create(user);
    auto email = Email { .id = SqlGuid::Create(), .address = "alice@example.com", .user = user };
    dm.Create(email);
    auto supplier = RrSupplier { .id = {}, .name = "Supplier", .rating = {} }; // no account, so no rating
    dm.Create(supplier);

    SECTION("BelongsTo whose referenced row was deleted after the read")
    {
        auto loaded = dm.QuerySingle<Email>(email.id.Value());
        REQUIRE(loaded.has_value());
        if (!loaded.has_value())
            return;
        CHECK(dm.Delete(email) == 1); // first, so the foreign key does not refuse deleting the user
        CHECK(dm.Delete(user) == 1);

        CHECK(loaded->user.Record().error() == RelationError::NotFound);
        CHECK(loaded->user.Record().error() == RelationError::NotFound);
    }
    SECTION("HasOneThrough with nothing on the far side")
    {
        auto loaded = dm.QuerySingle<RrSupplier>(supplier.id.Value());
        REQUIRE(loaded.has_value());
        if (!loaded.has_value())
            return;

        CHECK(loaded->rating.Record().error() == RelationError::NotFound);
        CHECK(loaded->rating.Record().error() == RelationError::NotFound);
    }
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
    CHECK(to_string(static_cast<RelationError>(0xFF)) == "Unknown");
    CHECK(SqlRequireLoadedError { "BelongsTo<X>", RelationError::NotFound }.Error() == RelationError::NotFound);
}
