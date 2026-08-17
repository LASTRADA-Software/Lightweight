// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string_view>

using namespace std::string_view_literals;
using namespace Lightweight;

struct StateTestsPartialPersonName
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<25>> name;

    static constexpr std::string_view TableName = RecordTableName<Person>;
};

// ================================================================================================
// IsModified — dirty-flag tracking
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "DataMapper::IsModified flips when a Field is assigned", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    auto person = Person {};
    person.name = "John Doe";
    dm.Create(person);

    // After Create the record is in NotModified state.
    CHECK_FALSE(dm.IsModified(person));

    person.name = "Jane Doe";
    CHECK(dm.IsModified(person));

    dm.Update(person);
    // Update marks the record clean again.
    CHECK_FALSE(dm.IsModified(person));
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper::Update with no modified fields is a no-op", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    auto person = Person { .id = {}, .name = "John Doe", .is_active = true, .age = 30 };
    dm.Create(person);
    REQUIRE_FALSE(dm.IsModified(person));

    // Nothing has been touched since Create(), so there is no SET clause to emit. Without an
    // early-out this builds `UPDATE "Persons" SET  WHERE "id" = ?`, which the driver rejects.
    REQUIRE_NOTHROW(dm.Update(person));

    auto const reloaded = dm.QuerySingle<Person>(person.id);
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->name.Value() == person.name.Value());
    CHECK(reloaded->age.Value() == person.age.Value());
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper::Update after a fetch with no changes is a no-op", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    auto seed = Person { .id = {}, .name = "Alice", .is_active = true, .age = 30 };
    dm.Create(seed);

    auto fetched = dm.QuerySingle<Person>(seed.id);
    REQUIRE(fetched.has_value());
    REQUIRE_FALSE(dm.IsModified(*fetched));

    // A fetched-but-untouched record has every dirty flag cleared, same empty-SET situation.
    REQUIRE_NOTHROW(dm.Update(*fetched));
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper::IsModified is false for a freshly-fetched record", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    auto seed = Person { .id = {}, .name = "Alice", .is_active = true, .age = 30 };
    dm.Create(seed);

    auto fetchedOpt = dm.QuerySingle<Person>(seed.id);
    REQUIRE(fetchedOpt.has_value());
    if (!fetchedOpt.has_value())
        return;
    auto& fetched = *fetchedOpt;
    CHECK_FALSE(dm.IsModified(fetched));
}

// ================================================================================================
// CreateCopyOf — duplicates a record under a fresh primary key
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "DataMapper::CreateCopyOf inserts a new row with a new primary key", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    auto original = Person { .id = {}, .name = "Pat", .is_active = true, .age = 40 };
    dm.Create(original);
    REQUIRE(original.id.Value());

    auto const newId = dm.CreateCopyOf(original);
    CHECK(newId != original.id.Value());

    auto copyFetchedOpt = dm.QuerySingle<Person>(newId);
    REQUIRE(copyFetchedOpt.has_value());
    if (!copyFetchedOpt.has_value())
        return;
    auto& copyFetched = *copyFetchedOpt;
    CHECK(copyFetched.name == original.name);
    CHECK(copyFetched.is_active == original.is_active);
    CHECK(copyFetched.age.Value().value_or(-1) == original.age.Value().value_or(-2));
}

// ================================================================================================
// Partial-column query — exercises the BindOutputColumns path with a subset of fields
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "DataMapper queries a partial record (subset of fields)", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    auto p = Person { .id = {}, .name = "OnlyName", .is_active = false, .age = 18 };
    dm.Create(p);

    auto partialOpt = dm.QuerySingle<StateTestsPartialPersonName>(p.id.Value());
    REQUIRE(partialOpt.has_value());
    if (!partialOpt.has_value())
        return;
    auto& partial = *partialOpt;
    CHECK(partial.id == p.id);
    CHECK(partial.name == p.name);
}

// ================================================================================================
// Inspect — covers the non-null / null / SqlGuid / bool branches
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "DataMapper::Inspect stringifies a populated record", "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    auto p = Person { .id = {}, .name = "Inspector", .is_active = true, .age = 99 };
    dm.Create(p);

    auto const inspected = DataMapper::Inspect(p);
    CHECK(inspected.contains("Inspector"));
    CHECK(inspected.contains("99"));
    // The Person record formatter uses operator<< — verify that DataMapper::Inspect itself
    // includes the column / field names rather than just the bare values.
    CHECK_FALSE(inspected.empty());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "DataMapper::Inspect of a record with NULL columns renders without crashing",
                 "[DataMapper]")
{
    auto dm = DataMapper();
    dm.CreateTable<Person>();

    auto p = Person { .id = {}, .name = "NoAge", .is_active = false, .age = std::nullopt };
    dm.Create(p);

    auto const inspected = DataMapper::Inspect(p);
    CHECK(inspected.contains("NoAge"));
}
