
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

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf: Create a copy of a record", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<TestRecord>();

    // Create original record
    auto original = TestRecord { .comment = "Original Record" };
    dm.Create(original);
    INFO("Created original record: " << DataMapper::Inspect(original));

    // Create a copy of the original record
    auto const copiedId = dm.CreateCopyOf(original);
    INFO("Copied record with new ID: " << copiedId);

    // Verify the copy has a different primary key
    REQUIRE(copiedId != original.id.Value());

    // Query the copied record and verify its data
    auto const copiedRecord = dm.QuerySingle<TestRecord>(copiedId).value();
    REQUIRE(copiedRecord.id == copiedId);
    REQUIRE(copiedRecord.comment == original.comment);
}

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf: Copy record with multiple fields", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    // Create original person
    auto original = Person { .name = "Jane Doe", .is_active = true, .age = 30 };
    dm.Create(original);
    INFO("Created original person: " << DataMapper::Inspect(original));

    // Create a copy
    auto const copiedId = dm.CreateCopyOf(original);
    INFO("Copied person with new ID: " << copiedId);

    // Verify the copy has different primary key but same data
    REQUIRE(copiedId != original.id.Value());

    auto const copied = dm.QuerySingle<Person>(copiedId).value();
    REQUIRE(copied.id == copiedId);
    REQUIRE(copied.name == original.name);
    REQUIRE(copied.is_active == original.is_active);
    REQUIRE(copied.age == original.age);
}

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf: Copy record with optional fields", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    // Create original person with null age
    auto original = Person { .name = "John Smith", .is_active = false, .age = std::nullopt };
    dm.Create(original);

    // Create a copy
    auto const copiedId = dm.CreateCopyOf(original);

    // Verify the copy
    auto const copied = dm.QuerySingle<Person>(copiedId).value();
    REQUIRE(copied.id != original.id);
    REQUIRE(copied.name == original.name);
    REQUIRE(copied.is_active == original.is_active);
    REQUIRE(!copied.age.Value().has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf: Multiple copies of the same record", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<TestRecord>();

    // Create original record
    auto original = TestRecord { .comment = "Multi-Copy Test" };
    dm.Create(original);

    // Create multiple copies
    auto const copy1Id = dm.CreateCopyOf(original);
    auto const copy2Id = dm.CreateCopyOf(original);
    auto const copy3Id = dm.CreateCopyOf(original);

    // Verify all copies have different primary keys
    REQUIRE(copy1Id != original.id.Value());
    REQUIRE(copy2Id != original.id.Value());
    REQUIRE(copy3Id != original.id.Value());
    REQUIRE(copy1Id != copy2Id);
    REQUIRE(copy1Id != copy3Id);
    REQUIRE(copy2Id != copy3Id);

    // Verify all copies have the same data
    auto const copy1 = dm.QuerySingle<TestRecord>(copy1Id).value();
    auto const copy2 = dm.QuerySingle<TestRecord>(copy2Id).value();
    auto const copy3 = dm.QuerySingle<TestRecord>(copy3Id).value();

    REQUIRE(copy1.comment == original.comment);
    REQUIRE(copy2.comment == original.comment);
    REQUIRE(copy3.comment == original.comment);
}

// NOLINTEND(bugprone-unchecked-optional-access)
