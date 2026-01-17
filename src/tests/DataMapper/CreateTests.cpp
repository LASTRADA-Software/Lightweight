
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
    INFO("record: " << DataMapper::Inspect(record));
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

TEST_CASE_METHOD(SqlTestFixture, "Create with defined primary key", "[DataMapper]")
{
    auto dm = DataMapper();

    dm.CreateTables<EntryWithIntPrimaryKey>();

    auto entry = EntryWithIntPrimaryKey { .id = 42, .comment = "The Answer" };
    dm.Create(entry);

    REQUIRE(entry.id.Value() == 42);

    entry.comment = "Updated Comment";
    dm.Update(entry);
    REQUIRE(entry.id.Value() == 42);
    REQUIRE(dm.QuerySingle<EntryWithIntPrimaryKey>(42).value().comment.Value() == "Updated Comment");
}

struct CopyTestAutoIncrement
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> name;
    Field<int> value;

    std::weak_ordering operator<=>(CopyTestAutoIncrement const& other) const = default;
};

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf: Auto-increment primary key", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<CopyTestAutoIncrement>();

    // Create original record
    auto original = CopyTestAutoIncrement { .id = 0, .name = "Original", .value = 42 };
    dm.Create(original);
    REQUIRE(original.id.Value() != 0);

    // Create a copy of the original record
    auto const newId = dm.CreateCopyOf(original);

    // Verify the copy has a different primary key
    CHECK(newId != original.id.Value());

    // Verify the copy has the same field values
    auto const copy = dm.QuerySingle<CopyTestAutoIncrement>(newId);
    REQUIRE(copy.has_value());
    CHECK(copy.value().name.Value() == original.name.Value());
    CHECK(copy.value().value.Value() == original.value.Value());

    // Verify original is unchanged
    auto const queriedOriginal = dm.QuerySingle<CopyTestAutoIncrement>(original.id);
    REQUIRE(queriedOriginal.has_value());
    CHECK(queriedOriginal.value().name.Value() == "Original");
    CHECK(queriedOriginal.value().value.Value() == 42);
}

struct CopyTestAutoAssign
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<30>> name;
    Field<int> value;

    std::weak_ordering operator<=>(CopyTestAutoAssign const& other) const = default;
};

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf: Auto-assign (GUID) primary key", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<CopyTestAutoAssign>();

    // Create original record
    auto original = CopyTestAutoAssign { .id = SqlGuid::Create(), .name = "Original", .value = 100 };
    dm.Create(original);
    REQUIRE(original.id.Value());

    // Create a copy of the original record
    auto const newId = dm.CreateCopyOf(original);

    // Verify the copy has a different primary key
    CHECK(newId != original.id.Value());

    // Verify the copy has the same field values
    auto const copy = dm.QuerySingle<CopyTestAutoAssign>(newId);
    REQUIRE(copy.has_value());
    CHECK(copy.value().name.Value() == original.name.Value());
    CHECK(copy.value().value.Value() == original.value.Value());

    // Verify both records exist
    CHECK(dm.Query<CopyTestAutoAssign>().Count() == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "CreateCopyOf: Multiple copies", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<CopyTestAutoIncrement>();

    // Create original record
    auto original = CopyTestAutoIncrement { .id = 0, .name = "Master", .value = 1 };
    dm.Create(original);

    // Create multiple copies
    auto const copy1Id = dm.CreateCopyOf(original);
    auto const copy2Id = dm.CreateCopyOf(original);
    auto const copy3Id = dm.CreateCopyOf(original);

    // Verify all copies have unique primary keys
    CHECK(copy1Id != original.id.Value());
    CHECK(copy2Id != original.id.Value());
    CHECK(copy3Id != original.id.Value());
    CHECK(copy1Id != copy2Id);
    CHECK(copy2Id != copy3Id);
    CHECK(copy1Id != copy3Id);

    // Verify total count
    CHECK(dm.Query<CopyTestAutoIncrement>().Count() == 4);

    // Verify all copies have the same field values
    auto const allRecords = dm.Query<CopyTestAutoIncrement>().All();
    for (auto const& record: allRecords)
    {
        CHECK(record.name.Value() == "Master");
        CHECK(record.value.Value() == 1);
    }
}

// NOLINTEND(bugprone-unchecked-optional-access)
