// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
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

// All columns fixed-width, including inline fixed-capacity strings (SqlAnsiString is a SqlFixedString
// specialization): eligible for the native zero-copy row-wise batch path.
struct BatchFixedStringRecord
{
    Field<int32_t, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<32>> name;
    Field<SqlAnsiString<16>> code;
};

// Nullable inline fixed-capacity string + fixed columns: exercises the native zero-copy path for
// std::optional<SqlAnsiString<N>> (per-row NULL-or-length row-strided indicator).
struct BatchOptionalFixedStringRecord
{
    Field<int32_t, PrimaryKey::AutoAssign> id;
    Field<std::optional<SqlAnsiString<32>>> name;
    Field<int32_t> count;
};

// Fixed-point numeric column: native path via the singular InputParameter bound with the type's
// compile-time precision/scale.
struct BatchNumericRecord
{
    Field<int32_t, PrimaryKey::AutoAssign> id;
    Field<SqlNumeric<10, 2>> amount;
};

// Temporal columns (all fixed-width): native path via the singular InputParameter.
struct BatchTemporalRecord
{
    Field<int32_t, PrimaryKey::AutoAssign> id;
    Field<SqlDate> date;
    Field<SqlTime> time;
    Field<SqlDateTime> when;
};

// Nullable timestamp: exercises std::optional<SqlDateTime> batch binding (NULL + value round-trip).
struct BatchOptionalTemporalRecord
{
    Field<int32_t, PrimaryKey::AutoAssign> id;
    Field<std::optional<SqlDateTime>> when;
};

// Server-side auto-increment PK + storable columns: the auto-increment PK must be excluded from the
// batched INSERT column set (shared IsBatchInsertColumn predicate) yet used as the UpdateAll WHERE key.
struct BatchMixedColumnRecord
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<std::string> label;
    Field<int32_t> number;
};

// Counts which batch execution path SqlStatement took: the native row-wise path emits a single
// OnExecuteBatch(), the soft fallback emits one OnExecute() per row.
class BatchPathCountingLogger: public SqlLogger::Null
{
  public:
    int executeBatchCount = 0;
    int executeCount = 0;

    void OnExecuteBatch() override
    {
        ++executeBatchCount;
    }
    void OnExecute(std::string_view const& /*query*/) override
    {
        ++executeCount;
    }
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

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.CreateAll: native fixed-capacity strings", "[DataMapper][batch]")
{
    // All currently supported backends (MS SQL Server, PostgreSQL, SQLite3) accept native parameter
    // arrays, so an all-fixed-width record — including inline fixed-capacity strings — must take the
    // native row-wise path (a single batched execute), not one execute per row.
    static_assert(sizeof(BatchFixedStringRecord) % alignof(SQLLEN) == 0,
                  "fixed-capacity-string record must satisfy the row-strided indicator alignment requirement");

    auto dm = DataMapper {};
    dm.CreateTable<BatchFixedStringRecord>();

    auto records = std::vector<BatchFixedStringRecord> {};
    records.push_back({ .id = 1, .name = "Alice", .code = "AAA" });
    records.push_back({ .id = 2, .name = "Bob", .code = "BB" });
    records.push_back({ .id = 3, .name = "Charlie Brown", .code = "CCCCCCC" });

    BatchPathCountingLogger logger;
    auto& previousLogger = SqlLogger::GetLogger();
    SqlLogger::SetLogger(logger);
    dm.CreateAll(records);
    SqlLogger::SetLogger(previousLogger);

    CHECK(logger.executeBatchCount == 1); // native row-wise path: exactly one batched execute
    CHECK(logger.executeCount == 0);      // and no per-row executes

    for (auto const& expected: records)
    {
        auto const actual = dm.QuerySingle<BatchFixedStringRecord>(expected.id.Value());
        REQUIRE(actual.has_value());
        CHECK(actual->name.Value() == expected.name.Value());
        CHECK(actual->code.Value() == expected.code.Value());
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "DataMapper.CreateAll/UpdateAll: native optional fixed-capacity string",
                 "[DataMapper][batch]")
{
    static_assert(sizeof(BatchOptionalFixedStringRecord) % alignof(SQLLEN) == 0,
                  "nullable fixed-capacity-string record must satisfy the row-strided indicator alignment requirement");

    auto dm = DataMapper {};
    dm.CreateTable<BatchOptionalFixedStringRecord>();

    auto records = std::vector<BatchOptionalFixedStringRecord> {};
    records.push_back({ .id = 1, .name = SqlAnsiString<32> { "Alice" }, .count = 10 });
    records.push_back({ .id = 2, .name = std::nullopt, .count = 20 });         // NULL
    records.push_back({ .id = 3, .name = SqlAnsiString<32> {}, .count = 30 }); // empty string
    records.push_back({ .id = 4, .name = SqlAnsiString<32> { "0123456789012345678901234567890" }, .count = 40 }); // 31 chars

    BatchPathCountingLogger logger;
    auto& previousLogger = SqlLogger::GetLogger();
    SqlLogger::SetLogger(logger);
    dm.CreateAll(records);
    SqlLogger::SetLogger(previousLogger);

    CHECK(logger.executeBatchCount == 1); // native row-wise path: exactly one batched execute
    CHECK(logger.executeCount == 0);      // and no per-row executes

    auto const verify = [&](std::vector<BatchOptionalFixedStringRecord> const& expectedRecords) {
        for (auto const& expected: expectedRecords)
        {
            auto const actual = dm.QuerySingle<BatchOptionalFixedStringRecord>(expected.id.Value());
            REQUIRE(actual.has_value());
            CHECK(actual->count.Value() == expected.count.Value());
            CHECK(actual->name.Value().has_value() == expected.name.Value().has_value());
            if (expected.name.Value().has_value())
                CHECK(actual->name.Value().value() == expected.name.Value().value());
        }
    };
    verify(records);

    // UpdateAll over the same record shape stays native; flip value<->NULL and mutate a fixed column.
    records[0].name = std::nullopt;                // value -> NULL
    records[1].name = SqlAnsiString<32> { "Bob" }; // NULL -> value
    records[3].count = 444;
    dm.UpdateAll(records);
    verify(records);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "DataMapper.CreateAll: SqlNumeric binds with the column's precision/scale",
                 "[DataMapper][batch]")
{
    // Row 0 is a default-constructed (never-assigned) SqlNumeric, whose sqlValue.precision/scale are 0.
    // The native row-wise batch binds a single descriptor (taken from row 0) for the whole array, so
    // before the fix this corrupted/rejected the assigned values in rows 1..n on SQL_C_NUMERIC backends.
    auto dm = DataMapper {};
    dm.CreateTable<BatchNumericRecord>();

    auto records = std::vector<BatchNumericRecord> {};
    records.push_back({ .id = 1, .amount = SqlNumeric<10, 2> {} }); // default-constructed
    records.push_back({ .id = 2, .amount = SqlNumeric<10, 2> { 12.34 } });
    records.push_back({ .id = 3, .amount = SqlNumeric<10, 2> { 99.99 } });

    dm.CreateAll(records);

    CHECK(dm.Query<BatchNumericRecord>().Count() == 3);
    // The assigned rows must round-trip exactly (mis-bound from row 0's precision before the fix).
    CHECK(dm.QuerySingle<BatchNumericRecord>(2).value().amount.Value().ToString() == SqlNumeric<10, 2> { 12.34 }.ToString());
    CHECK(dm.QuerySingle<BatchNumericRecord>(3).value().amount.Value().ToString() == SqlNumeric<10, 2> { 99.99 }.ToString());
    REQUIRE(dm.QuerySingle<BatchNumericRecord>(1).has_value()); // default row present
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.CreateAll: native temporal columns", "[DataMapper][batch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<BatchTemporalRecord>();

    auto const makeRecord = [](int32_t id, unsigned day) {
        return BatchTemporalRecord {
            .id = id,
            .date = SqlDate { std::chrono::year { 2026 }, std::chrono::month { 5 }, std::chrono::day { day } },
            .time = SqlTime { std::chrono::hours { 13 }, std::chrono::minutes { 14 }, std::chrono::seconds { 15 } },
            .when = SqlDateTime { std::chrono::year { 2026 },
                                  std::chrono::month { 5 },
                                  std::chrono::day { day },
                                  std::chrono::hours { 8 },
                                  std::chrono::minutes { 30 },
                                  std::chrono::seconds { 45 },
                                  std::chrono::nanoseconds { 0 } },
        };
    };
    auto records = std::vector<BatchTemporalRecord> {};
    records.push_back(makeRecord(1, 6));
    records.push_back(makeRecord(2, 7));
    records.push_back(makeRecord(3, 8));

    BatchPathCountingLogger logger;
    auto& previousLogger = SqlLogger::GetLogger();
    SqlLogger::SetLogger(logger);
    dm.CreateAll(records);
    SqlLogger::SetLogger(previousLogger);

    CHECK(logger.executeBatchCount == 1); // native row-wise path (date/time bind by address, no indicators)
    CHECK(logger.executeCount == 0);

    for (auto const& expected: records)
    {
        auto const actual = dm.QuerySingle<BatchTemporalRecord>(expected.id.Value());
        REQUIRE(actual.has_value());
        CHECK(actual->date.Value() == expected.date.Value());
        CHECK(actual->time.Value() == expected.time.Value());
        CHECK(actual->when.Value() == expected.when.Value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.CreateAll: optional timestamp NULL and value round-trip", "[DataMapper][batch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<BatchOptionalTemporalRecord>();

    auto records = std::vector<BatchOptionalTemporalRecord> {};
    records.push_back({ .id = 1,
                        .when = SqlDateTime { std::chrono::year { 2026 },
                                              std::chrono::month { 1 },
                                              std::chrono::day { 2 },
                                              std::chrono::hours { 3 },
                                              std::chrono::minutes { 4 },
                                              std::chrono::seconds { 5 },
                                              std::chrono::nanoseconds { 0 } } });
    records.push_back({ .id = 2, .when = std::nullopt });
    dm.CreateAll(records);

    auto const engaged = dm.QuerySingle<BatchOptionalTemporalRecord>(1);
    REQUIRE(engaged.has_value());
    REQUIRE(engaged->when.Value().has_value());
    CHECK(engaged->when.Value().value() == records[0].when.Value().value());

    auto const disengaged = dm.QuerySingle<BatchOptionalTemporalRecord>(2);
    REQUIRE(disengaged.has_value());
    CHECK(!disengaged->when.Value().has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.UpdateAll: zero-row match does not throw (native)", "[DataMapper][batch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<BatchFixedRecord>();

    // Table is empty: a searched UPDATE matches no rows and the driver returns SQL_NO_DATA. UpdateAll
    // must tolerate it (like single Update()), not throw.
    auto records = std::vector<BatchFixedRecord> {};
    records.push_back({ .id = 100, .value = 1.0, .count = 1 });
    records.push_back({ .id = 200, .value = 2.0, .count = 2 });
    CHECK_NOTHROW(dm.UpdateAll(records));
    CHECK(dm.Query<BatchFixedRecord>().Count() == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.UpdateAll: soft path tolerates non-matching rows", "[DataMapper][batch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<BatchAggregateRecord>();

    auto seed = std::vector<BatchAggregateRecord> {};
    seed.push_back({ .id = 1, .name = std::string { "one" }, .maybe = 1 });
    dm.CreateAll(seed);

    auto updates = std::vector<BatchAggregateRecord> {};
    updates.push_back({ .id = 1, .name = std::string { "updated" }, .maybe = 2 }); // matches
    updates.push_back({ .id = 999, .name = std::string { "ghost" }, .maybe = 3 }); // no such row
    CHECK_NOTHROW(dm.UpdateAll(updates));

    auto const row1 = dm.QuerySingle<BatchAggregateRecord>(1);
    REQUIRE(row1.has_value());
    CHECK(row1->name.Value() == "updated");
    CHECK(!dm.QuerySingle<BatchAggregateRecord>(999).has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper.CreateAll: excludes server-side auto-increment PK", "[DataMapper][batch]")
{
    // IsBatchInsertColumn must exclude the ServerSideAutoIncrement id; otherwise every row would bind
    // id = 0 and collide on the primary key (or desync the bound `?` count from the value accessors).
    auto dm = DataMapper {};
    dm.CreateTable<BatchMixedColumnRecord>();

    auto records = std::vector<BatchMixedColumnRecord> {};
    records.push_back({ .id = {}, .label = std::string { "alpha" }, .number = 1 });
    records.push_back({ .id = {}, .label = std::string { "beta" }, .number = 2 });
    records.push_back({ .id = {}, .label = std::string { "gamma" }, .number = 3 });

    CHECK_NOTHROW(dm.CreateAll(records));
    CHECK(dm.Query<BatchMixedColumnRecord>().Count() == 3);
}

// NOLINTEND(bugprone-unchecked-optional-access)
