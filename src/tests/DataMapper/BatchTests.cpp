// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <optional>
#include <ranges>
#include <string>
#include <vector>

using namespace Lightweight;

// NOLINTBEGIN(bugprone-unchecked-optional-access)

// Record types must have linkage (not in an anonymous namespace) for reflection-cpp.

// All fixed-width columns: CreateAll/UpdateAll take the native zero-copy row-wise batch path.
struct BatchFixedRecord
{
    Field<int64_t, PrimaryKey::AutoAssign> id;
    Field<double> value;
    Field<int32_t> count;
};

// Contains a std::string (aggregate member) and a nullable column: routed through the soft batch path.
struct BatchAggregateRecord
{
    Field<int32_t, PrimaryKey::AutoAssign> id;
    Field<std::string> name;
    Field<std::optional<int32_t>> maybe;
};

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.CreateAll: native (all fixed-width)", "[DataMapper][batch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<BatchFixedRecord>();

    auto records = std::vector<BatchFixedRecord> {};
    for (auto const i: std::views::iota(1, 6))
        records.push_back({ .id = i, .value = i * 1.5, .count = i * 10 });

    dm.CreateAll(records);

    CHECK(dm.Query<BatchFixedRecord>().Count() == 5);
    for (auto const& expected: records)
    {
        auto const actual = dm.QuerySingle<BatchFixedRecord>(expected.id.Value());
        REQUIRE(actual.has_value());
        CHECK(actual->id.Value() == expected.id.Value());
        CHECK_THAT(actual->value.Value(), Catch::Matchers::WithinAbs(expected.value.Value(), 0.000'001));
        CHECK(actual->count.Value() == expected.count.Value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.CreateAll: soft (std::string + optional)", "[DataMapper][batch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<BatchAggregateRecord>();

    auto records = std::vector<BatchAggregateRecord> {};
    records.push_back({ .id = 1, .name = std::string {}, .maybe = 11 });                    // empty string
    records.push_back({ .id = 2, .name = std::string { "Alice" }, .maybe = std::nullopt }); // NULL
    records.push_back({ .id = 3, .name = std::string { "a longer aggregate value" }, .maybe = 33 });

    dm.CreateAll(records);

    CHECK(dm.Query<BatchAggregateRecord>().Count() == 3);
    for (auto const& expected: records)
    {
        auto const actual = dm.QuerySingle<BatchAggregateRecord>(expected.id.Value());
        REQUIRE(actual.has_value());
        CHECK(actual->name.Value() == expected.name.Value());
        CHECK(actual->maybe.Value() == expected.maybe.Value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.CreateAll: empty and single span", "[DataMapper][batch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<BatchFixedRecord>();

    SECTION("empty span is a no-op")
    {
        auto const empty = std::vector<BatchFixedRecord> {};
        dm.CreateAll(empty);
        CHECK(dm.Query<BatchFixedRecord>().Count() == 0);
    }

    SECTION("single-record span")
    {
        auto const one = std::vector<BatchFixedRecord> { { .id = 42, .value = 4.2, .count = 420 } };
        dm.CreateAll(one);
        CHECK(dm.Query<BatchFixedRecord>().Count() == 1);
        CHECK(dm.QuerySingle<BatchFixedRecord>(42).value().count.Value() == 420);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.UpdateAll: writes all columns incl. NULL", "[DataMapper][batch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<BatchAggregateRecord>();

    auto records = std::vector<BatchAggregateRecord> {};
    records.push_back({ .id = 1, .name = std::string { "one" }, .maybe = 1 });
    records.push_back({ .id = 2, .name = std::string { "two" }, .maybe = 2 });
    records.push_back({ .id = 3, .name = std::string { "three" }, .maybe = 3 });
    dm.CreateAll(records);

    // Mutate: change names, and set one column to NULL.
    records[0].name = std::string { "ONE" };
    records[1].maybe = std::nullopt;
    records[2].name = std::string { "THREE" };
    records[2].maybe = 30;
    dm.UpdateAll(records);

    for (auto const& expected: records)
    {
        auto const actual = dm.QuerySingle<BatchAggregateRecord>(expected.id.Value());
        REQUIRE(actual.has_value());
        CHECK(actual->name.Value() == expected.name.Value());
        CHECK(actual->maybe.Value() == expected.maybe.Value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.CreateAll then single Create reuses statement safely", "[DataMapper][batch]")
{
    // Regression: CreateAll uses native row-wise array binding (PARAMSET_SIZE/PARAM_BIND_TYPE on the
    // shared statement); a following single Create() must insert exactly one row.
    auto dm = DataMapper {};
    dm.CreateTable<BatchFixedRecord>();

    auto records = std::vector<BatchFixedRecord> {};
    records.push_back({ .id = 1, .value = 1.5, .count = 10 });
    records.push_back({ .id = 2, .value = 2.5, .count = 20 });
    dm.CreateAll(records);
    REQUIRE(dm.Query<BatchFixedRecord>().Count() == 2);

    auto single = BatchFixedRecord { .id = 99, .value = 9.5, .count = 990 };
    dm.CreateExplicit(single);

    CHECK(dm.Query<BatchFixedRecord>().Count() == 3);
    CHECK(dm.QuerySingle<BatchFixedRecord>(99).value().count.Value() == 990);
}

// NOLINTEND(bugprone-unchecked-optional-access)
