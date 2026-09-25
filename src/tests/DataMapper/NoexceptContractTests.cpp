// SPDX-License-Identifier: Apache-2.0

// A relation accessor runs its lazy loader on first touch, and the loader's query can fail for any
// runtime reason (lost connection, a busy connection on SQL Server without MARS, a dropped table, ...).
// Such a failure must reach the caller as an ordinary value or exception - never std::terminate from a
// noexcept boundary. The accessors returning RelationResult report it as RelationError::QueryFailed;
// the shortcuts that cannot (operator->, operator*, begin()/end(), At()) throw SqlRequireLoadedError.
// Each test drops the tables the loader is about to read.

#include "../Utils.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <variant>

using namespace Lightweight;

struct NxSupplier;
struct NxAccount;
struct NxAccountHistory;

struct NxSupplier
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};
    HasOneThrough<NxAccountHistory, Through<NxAccount>> accountHistory {};
};

struct NxAccount
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> iban {};
    BelongsTo<Member(NxSupplier::id)> supplier {};
};

struct NxAccountHistory
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<int> creditRating {};
    BelongsTo<Member(NxAccount::id)> account {};
};

// Compile-time half: none of these may promise not to throw.
static_assert(!noexcept(std::declval<decltype(Email::user)&>().operator*()));
static_assert(!noexcept(std::declval<decltype(Email::user) const&>().operator*()));
static_assert(!noexcept(std::declval<decltype(User::emails) const&>().Count()));
static_assert(!noexcept(std::declval<decltype(User::emails) const&>().IsEmpty()));
static_assert(!noexcept(std::declval<decltype(Physician::patients)&>().All()));
static_assert(!noexcept(std::declval<decltype(Physician::patients) const&>().All()));
static_assert(!noexcept(std::declval<decltype(Physician::patients)&>().begin()));
static_assert(!noexcept(std::declval<decltype(Physician::patients)&>().end()));
static_assert(!noexcept(std::declval<decltype(Physician::patients) const&>().begin()));
static_assert(!noexcept(std::declval<decltype(Physician::patients) const&>().end()));
static_assert(!noexcept(std::declval<decltype(NxSupplier::accountHistory)&>().Record()));
static_assert(!noexcept(std::declval<decltype(NxSupplier::accountHistory) const&>().Record()));
static_assert(!noexcept(std::declval<decltype(NxSupplier::accountHistory)&>().operator*()));
static_assert(!noexcept(std::declval<decltype(NxSupplier::accountHistory) const&>().operator*()));
static_assert(!noexcept(std::declval<decltype(NxSupplier::accountHistory)&>().operator->()));
static_assert(!noexcept(std::declval<decltype(NxSupplier::accountHistory) const&>().operator->()));
static_assert(!noexcept(std::declval<SqlVariant&>().Get<int>()));
static_assert(!noexcept(std::declval<SqlVariantRowIterator&>().operator++()));
static_assert(!noexcept(std::declval<SqlVariantRowCursor&>().begin()));
static_assert(!noexcept(std::declval<SqlRowIterator<User>::iterator&>().operator*()));
static_assert(!noexcept(std::declval<SqlMigration::MigrationManager const&>().GetPending()));

namespace
{
// Drops @p Root and every table referencing it, dependants first, so the loaders below find nothing.
template <typename Root>
void DropTableTree(DataMapper& dm)
{
    auto stmt = SqlStatement { dm.Connection() };
    std::ignore = SqlTestFixture::DropTableRecursively(
        stmt, { .catalog = {}, .schema = {}, .table = std::string { RecordTableName<Root> } });
}
} // namespace

TEST_CASE_METHOD(SqlTestFixture, "noexcept contract: BelongsTo::operator* reports a failed load", "[DataMapper][noexcept]")
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
    DropTableTree<User>(dm);

    CHECK(loaded->user.Record().error() == RelationError::QueryFailed);
    CHECK_THROWS_AS(std::ignore = *loaded->user, SqlRequireLoadedError);
}

TEST_CASE_METHOD(SqlTestFixture, "noexcept contract: HasMany::Count/IsEmpty report a failed load", "[DataMapper][noexcept]")
{
    auto dm = DataMapper {};
    dm.CreateTables<User, Email>();
    auto user = User { .id = SqlGuid::Create(), .name = "Alice", .emails = {} };
    dm.Create(user);

    auto loaded = dm.QuerySingle<User>(user.id.Value());
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
        return;
    DropTableTree<User>(dm);

    CHECK(loaded->emails.Count().error() == RelationError::QueryFailed);
    CHECK(loaded->emails.IsEmpty().error() == RelationError::QueryFailed);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "noexcept contract: HasManyThrough::All/begin/end report a failed load",
                 "[DataMapper][noexcept]")
{
    auto dm = DataMapper {};
    dm.CreateTables<Physician, Patient, Appointment>();
    auto physician = Physician { .id = SqlGuid::Create(), .name = "Dr. X", .appointments = {}, .patients = {} };
    dm.Create(physician);

    auto loaded = dm.QuerySingle<Physician>(physician.id.Value());
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
        return;
    DropTableTree<Physician>(dm);

    SECTION("All")
    {
        CHECK(loaded->patients.All().error() == RelationError::QueryFailed);
    }
    SECTION("begin")
    {
        CHECK_THROWS_AS(std::ignore = loaded->patients.begin(), SqlRequireLoadedError);
    }
    SECTION("end")
    {
        CHECK_THROWS_AS(std::ignore = std::as_const(loaded->patients).end(), SqlRequireLoadedError);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "noexcept contract: HasOneThrough accessors report a failed load", "[DataMapper][noexcept]")
{
    auto dm = DataMapper {};
    dm.CreateTables<NxSupplier, NxAccount, NxAccountHistory>();
    auto supplier = NxSupplier { .id = {}, .name = "Supplier", .accountHistory = {} };
    dm.Create(supplier);

    auto loaded = dm.QuerySingle<NxSupplier>(supplier.id.Value());
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
        return;
    DropTableTree<NxSupplier>(dm);

    SECTION("Record")
    {
        CHECK(loaded->accountHistory.Record().error() == RelationError::QueryFailed);
    }
    SECTION("operator*")
    {
        CHECK_THROWS_AS(std::ignore = *loaded->accountHistory, SqlRequireLoadedError);
    }
    SECTION("operator->")
    {
        CHECK_THROWS_AS(std::ignore = loaded->accountHistory->creditRating, SqlRequireLoadedError);
    }
}

TEST_CASE("noexcept contract: SqlVariant::Get of an absent alternative throws", "[SqlVariant][noexcept]")
{
    auto variant = SqlVariant { std::string { "text" } };
    CHECK_THROWS_AS(std::ignore = variant.Get<int>(), std::bad_variant_access);
}
