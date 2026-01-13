
// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "Entities.hpp"
#include "Lightweight/DataMapper/QueryBuilders.hpp"

#include <Lightweight/Lightweight.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string_view>

using namespace std::string_view_literals;
using namespace std::string_literals;
using namespace Lightweight;

struct RecordWithDefaults
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> name1 { "John Doe" };
    Field<std::optional<SqlAnsiString<30>>> name2 { "John Doe" };
    Field<bool> boolean1 { true };
    Field<bool> boolean2 { false };
    Field<std::optional<int>> int1 { 42 };
    Field<std::optional<int>> int2 {};
};

struct RecordWithNoDefaults
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> name1;
    Field<std::optional<SqlAnsiString<30>>> name2;
    Field<bool> boolean1;
    Field<bool> boolean2;
    Field<std::optional<int>> int1;
    Field<std::optional<int>> int2;

    static constexpr std::string_view TableName = RecordTableName<RecordWithDefaults>;
};

// NOLINTBEGIN(bugprone-unchecked-optional-access)

TEST_CASE_METHOD(SqlTestFixture, "Create table with default values", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<RecordWithDefaults>();

    auto record = RecordWithDefaults {};
    dm.Create(record);

    auto const actual = dm.QuerySingle<RecordWithNoDefaults>(record.id).value();
    CHECK(actual.id == record.id);
    CHECK(actual.name1 == record.name1);
    CHECK(actual.boolean1 == record.boolean1);
    CHECK(actual.boolean2 == record.boolean2);
    CHECK(actual.int1 == record.int1);
    CHECK(actual.int2 == record.int2);
}

TEST_CASE_METHOD(SqlTestFixture, "Check that record exist", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<RecordWithDefaults>();

    auto record = RecordWithDefaults {};
    dm.Create(record);

    CHECK(dm.Query<RecordWithDefaults>().Where(FieldNameOf<Member(RecordWithDefaults::id)>, "=", record.id.Value()).Exist());
    CHECK(!dm.Query<RecordWithDefaults>().Where(FieldNameOf<Member(RecordWithDefaults::id)>, "=", -1).Exist());
}

struct TestRecord
{
    Field<uint64_t, PrimaryKey::AutoAssign> id {};
    Field<SqlAnsiString<30>> comment;

    std::weak_ordering operator<=>(TestRecord const& other) const = default;
};

std::ostream& operator<<(std::ostream& os, TestRecord const& record)
{
    return os << DataMapper::Inspect(record);
}

TEST_CASE_METHOD(SqlTestFixture, "PrimaryKey: AutoAssign", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<TestRecord>();

    auto record = TestRecord { .comment = "Hello, World!" };
    dm.Create(record);
    INFO("Created record: " << DataMapper::Inspect(record));
    auto const queriedRecord = dm.QuerySingle<TestRecord>(record.id).value();
    REQUIRE(queriedRecord == record);

    auto record2 = TestRecord { .comment = "Hello, World! 2" };
    dm.Create(record2);
    INFO("Created record: " << DataMapper::Inspect(record2));
    auto const queriedRecord2 = dm.QuerySingle<TestRecord>(record2.id).value();
    REQUIRE(queriedRecord2 == record2);

    REQUIRE(record.id != record2.id);
}

struct MultiPkRecord
{
    Field<SqlAnsiString<32>, PrimaryKey::AutoAssign> firstName;
    Field<SqlAnsiString<32>, PrimaryKey::AutoAssign> lastName;

    constexpr std::weak_ordering operator<=>(MultiPkRecord const& other) const = default;
};

TEST_CASE_METHOD(SqlTestFixture, "Table with multiple primary keys", "[DataMapper]")
{
    auto dm = DataMapper();

    dm.CreateTable<MultiPkRecord>();

    auto record = MultiPkRecord { .firstName = "John", .lastName = "Doe" };
    dm.Create(record);

    {
        auto const _ = ScopedSqlNullLogger {}; // Suppress the error message, as we are testing for it
        CHECK_THROWS_AS(dm.CreateExplicit(MultiPkRecord { .firstName = "John", .lastName = "Doe" }), SqlException);
    }

    auto queriedRecords = dm.Query<MultiPkRecord>().All();

    CHECK(queriedRecords.size() == 1);
    auto const& queriedRecord = queriedRecords.at(0);
    INFO("Queried record: " << DataMapper::Inspect(queriedRecord));
    CHECK(queriedRecord == record);
}

TEST_CASE_METHOD(SqlTestFixture, "Loading of the dependent records after create", "[DataMapper]")
{
    auto dm = DataMapper();

    dm.CreateTables<User, NullableForeignKeyUser>();

    auto user = User { .id = SqlGuid::Create(), .name = "John Doe" };
    dm.Create(user);

    auto nullableFKUser = NullableForeignKeyUser { .user = user };
    dm.Create(nullableFKUser);
    REQUIRE(nullableFKUser.user.Value().has_value());
    REQUIRE(nullableFKUser.user.Record().has_value());

    auto nullableFKUserNotSet = NullableForeignKeyUser {};
    dm.Create<Light::DataMapperOptions{.loadRelations = false}>(nullableFKUserNotSet);
    REQUIRE(!nullableFKUserNotSet.user.Value().has_value());
    REQUIRE(!nullableFKUserNotSet.user.Record().has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf with auto-increment primary key", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<RecordWithDefaults>();

    // Create the original record
    auto original = RecordWithDefaults {};
    original.name1 = "Alice";
    original.boolean1 = false;
    original.int1 = 123;
    dm.Create(original);

    // Create a copy of the original record
    auto const copiedId = dm.CreateCopyOf(original);

    // Verify that the copy has a different primary key
    REQUIRE(copiedId != original.id.Value());

    // Query the copied record from the database
    auto const copied = dm.QuerySingle<RecordWithDefaults>(copiedId).value();

    // Verify that the copied record has the same field values as the original
    CHECK(copied.name1 == original.name1);
    CHECK(copied.name2 == original.name2);
    CHECK(copied.boolean1 == original.boolean1);
    CHECK(copied.boolean2 == original.boolean2);
    CHECK(copied.int1 == original.int1);
    CHECK(copied.int2 == original.int2);

    // Verify that the copied record exists in the database with a different ID
    CHECK(copied.id != original.id);
}

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf with auto-assign primary key (GUID)", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    // Create the original record
    auto original = Person { .name = "Bob", .is_active = true, .age = 30 };
    dm.Create(original);

    // Create a copy of the original record
    auto const copiedId = dm.CreateCopyOf(original);

    // Verify that the copy has a different primary key
    REQUIRE(copiedId != original.id.Value());

    // Query the copied record from the database
    auto const copied = dm.QuerySingle<Person>(copiedId).value();

    // Verify that the copied record has the same field values as the original
    CHECK(copied.name == original.name);
    CHECK(copied.is_active == original.is_active);
    CHECK(copied.age == original.age);

    // Verify that the copied record exists in the database with a different ID
    CHECK(copied.id != original.id);
}

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf with multiple primary keys", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<MultiPkRecord>();

    // Create the original record
    auto original = MultiPkRecord { .firstName = "John", .lastName = "Doe" };
    dm.Create(original);

    // For records with manually assigned primary keys, CreateCopyOf will keep the same primary keys
    // This will likely cause a unique constraint violation, so we expect an exception
    auto const _ = ScopedSqlNullLogger {}; // Suppress the error message
    CHECK_THROWS_AS(dm.CreateCopyOf(original), SqlException);
}

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf multiple times", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    // Create the original record
    auto original = Person { .name = "Charlie", .is_active = false, .age = 25 };
    dm.Create(original);

    // Create multiple copies
    auto const copy1Id = dm.CreateCopyOf(original);
    auto const copy2Id = dm.CreateCopyOf(original);
    auto const copy3Id = dm.CreateCopyOf(original);

    // Verify that all copies have different primary keys
    REQUIRE(copy1Id != original.id.Value());
    REQUIRE(copy2Id != original.id.Value());
    REQUIRE(copy3Id != original.id.Value());
    REQUIRE(copy1Id != copy2Id);
    REQUIRE(copy2Id != copy3Id);
    REQUIRE(copy1Id != copy3Id);

    // Query all records from the database
    auto const allRecords = dm.Query<Person>().All();
    REQUIRE(allRecords.size() == 4); // original + 3 copies

    // Verify that all copies have the same field values as the original
    for (auto const& record: allRecords)
    {
        CHECK(record.name == original.name);
        CHECK(record.is_active == original.is_active);
        CHECK(record.age == original.age);
    }
}

// NOLINTEND(bugprone-unchecked-optional-access)
