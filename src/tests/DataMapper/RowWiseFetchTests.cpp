// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

using namespace Lightweight;

// NOLINTBEGIN(bugprone-unchecked-optional-access)

// Record types must have linkage (not in an anonymous namespace) for reflection-cpp.

// All fixed-width columns: Query<>().All()/Range() take the native row-wise array-fetch fast path.
struct RowFixedRecord
{
    Field<int64_t, PrimaryKey::AutoAssign> id;
    Field<int64_t> big;
    Field<double> ratio;
    Field<int32_t> mid;
    Field<int16_t> tiny;

    bool operator==(RowFixedRecord const&) const = default;
};

// Nullable fixed-width columns: exercises the pre-engage + reset-on-NULL row-wise output path.
struct RowNullableRecord
{
    Field<int64_t, PrimaryKey::AutoAssign> id;
    Field<std::optional<int32_t>> maybeInt;
    Field<std::optional<double>> maybeRatio;
};

// Temporal columns incl. a nullable timestamp: native row-wise fetch via SqlDate/SqlDateTime binders.
struct RowTemporalRecord
{
    Field<int64_t, PrimaryKey::AutoAssign> id;
    Field<SqlDate> date;
    Field<std::optional<SqlDateTime>> when;
};

// Inline fixed-capacity char strings (SqlAnsiString is SqlFixedString<N, char>), incl. a nullable one:
// row-wise fetchable via the SQL_C_CHAR inline bind + per-row length/trim fixup (per-row fallback on PG).
struct RowFixedStringRecord
{
    Field<int64_t, PrimaryKey::AutoAssign> id;
    Field<SqlAnsiString<32>> name;
    Field<std::optional<SqlAnsiString<16>>> code;
    Field<int32_t> number;
};

// Contains a std::string (growable): NOT row-wise fetchable, must fall back to the per-row path.
struct RowStringRecord
{
    Field<int64_t, PrimaryKey::AutoAssign> id;
    Field<std::string> name;
    Field<int32_t> number;
};

// The eligibility predicate must match the design exactly: fixed-width (incl. nullable fixed and inline
// fixed-capacity strings) records are row-wise fetchable; a record carrying a growable string is not.
static_assert(detail::CanRowWiseFetchRecord<RowFixedRecord>());
static_assert(detail::CanRowWiseFetchRecord<RowNullableRecord>());
static_assert(detail::CanRowWiseFetchRecord<RowTemporalRecord>());
static_assert(detail::CanRowWiseFetchRecord<RowFixedStringRecord>());
static_assert(!detail::CanRowWiseFetchRecord<RowStringRecord>());

// The fixed-string record is flagged as narrow-text-bearing (so PostgreSQL falls back), the others not.
static_assert(detail::RecordHasNarrowFixedStringColumn<RowFixedStringRecord>());
static_assert(!detail::RecordHasNarrowFixedStringColumn<RowFixedRecord>());

// Counts block-fetch round-trips: the row-wise fast path emits one OnFetchRow per SQLFetchScroll block,
// the per-row fallback emits one per row — so the count proves which path actually ran.
class FetchBlockCountingLogger: public SqlLogger::Null
{
  public:
    int fetchEvents = 0;

    void OnFetchRow() override
    {
        ++fetchEvents;
    }
};

template <typename Fn>
int CountFetchEvents(Fn&& fn)
{
    FetchBlockCountingLogger logger;
    auto& previousLogger = SqlLogger::GetLogger();
    SqlLogger::SetLogger(logger);
    std::forward<Fn>(fn)();
    SqlLogger::SetLogger(previousLogger);
    return logger.fetchEvents;
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RowWiseFetch: All() materializes fixed-width records in row blocks",
                 "[DataMapper][rowwisefetch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<RowFixedRecord>();

    auto seed = std::vector<RowFixedRecord> {};
    for (auto const i: std::views::iota(1, 51))
        seed.push_back({ .id = i, .big = i * 1000, .ratio = i * 1.5, .mid = i * 10, .tiny = static_cast<int16_t>(i) });
    dm.CreateAll(seed);

    std::vector<RowFixedRecord> records;
    auto const fetchEvents = CountFetchEvents([&] { records = dm.Query<RowFixedRecord>().All(); });

    // 50 rows fit in a single SQLFetchScroll block (default depth 1024): exactly one block-fetch, not 50.
    CHECK(fetchEvents == 1);

    REQUIRE(records.size() == 50);
    std::map<int64_t, RowFixedRecord> byId;
    for (auto const& r: records)
        byId.emplace(r.id.Value(), r);
    for (auto const& expected: seed)
    {
        auto const it = byId.find(expected.id.Value());
        REQUIRE(it != byId.end());
        CHECK(it->second.big.Value() == expected.big.Value());
        CHECK_THAT(it->second.ratio.Value(), Catch::Matchers::WithinAbs(expected.ratio.Value(), 0.000'001));
        CHECK(it->second.mid.Value() == expected.mid.Value());
        CHECK(it->second.tiny.Value() == expected.tiny.Value());
    }
}

TEST_CASE_METHOD(SqlTestFixture, "RowWiseFetch: NULLs in nullable columns round-trip", "[DataMapper][rowwisefetch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<RowNullableRecord>();

    auto seed = std::vector<RowNullableRecord> {};
    seed.push_back({ .id = 1, .maybeInt = 42, .maybeRatio = 3.5 });
    seed.push_back({ .id = 2, .maybeInt = std::nullopt, .maybeRatio = 9.0 });          // NULL int
    seed.push_back({ .id = 3, .maybeInt = 7, .maybeRatio = std::nullopt });            // NULL ratio
    seed.push_back({ .id = 4, .maybeInt = std::nullopt, .maybeRatio = std::nullopt }); // all NULL
    dm.CreateAll(seed);

    auto const records = dm.Query<RowNullableRecord>().All();
    REQUIRE(records.size() == 4);

    std::map<int64_t, RowNullableRecord const*> byId;
    for (auto const& r: records)
        byId.emplace(r.id.Value(), &r);

    CHECK(byId[1]->maybeInt.Value() == std::optional<int32_t> { 42 });
    CHECK(byId[1]->maybeRatio.Value() == std::optional<double> { 3.5 });
    CHECK_FALSE(byId[2]->maybeInt.Value().has_value());
    CHECK(byId[2]->maybeRatio.Value() == std::optional<double> { 9.0 });
    CHECK(byId[3]->maybeInt.Value() == std::optional<int32_t> { 7 });
    CHECK_FALSE(byId[3]->maybeRatio.Value().has_value());
    CHECK_FALSE(byId[4]->maybeInt.Value().has_value());
    CHECK_FALSE(byId[4]->maybeRatio.Value().has_value());
}

TEST_CASE_METHOD(SqlTestFixture, "RowWiseFetch: temporal columns and nullable timestamp", "[DataMapper][rowwisefetch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<RowTemporalRecord>();

    auto seed = std::vector<RowTemporalRecord> {};
    seed.push_back({ .id = 1,
                     .date = SqlDate { std::chrono::year { 2026 }, std::chrono::month { 6 }, std::chrono::day { 22 } },
                     .when = SqlDateTime { std::chrono::year { 2026 },
                                           std::chrono::month { 6 },
                                           std::chrono::day { 22 },
                                           std::chrono::hours { 8 },
                                           std::chrono::minutes { 30 },
                                           std::chrono::seconds { 45 },
                                           std::chrono::nanoseconds { 0 } } });
    seed.push_back({ .id = 2,
                     .date = SqlDate { std::chrono::year { 1999 }, std::chrono::month { 12 }, std::chrono::day { 31 } },
                     .when = std::nullopt });
    dm.CreateAll(seed);

    auto const records = dm.Query<RowTemporalRecord>().All();
    REQUIRE(records.size() == 2);

    std::map<int64_t, RowTemporalRecord const*> byId;
    for (auto const& r: records)
        byId.emplace(r.id.Value(), &r);

    CHECK(byId[1]->date.Value() == seed[0].date.Value());
    REQUIRE(byId[1]->when.Value().has_value());
    CHECK(byId[1]->when.Value().value() == seed[0].when.Value().value());
    CHECK(byId[2]->date.Value() == seed[1].date.Value());
    CHECK_FALSE(byId[2]->when.Value().has_value());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RowWiseFetch: block boundary across multiple SQLFetchScroll calls",
                 "[DataMapper][rowwisefetch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<RowFixedRecord>();

    // Two full blocks plus a partial last block proves the grow-and-rebind loop and the trim of the
    // final partial block (rowcount % depth != 0).
    constexpr auto depth = detail::kDefaultRowArrayFetchDepth;
    auto const total = static_cast<int>(depth * 2 + 7);

    auto seed = std::vector<RowFixedRecord> {};
    for (auto const i: std::views::iota(1, total + 1))
        seed.push_back({ .id = i, .big = i, .ratio = i * 0.5, .mid = i, .tiny = static_cast<int16_t>(i % 100) });
    dm.CreateAll(seed);

    std::vector<RowFixedRecord> records;
    auto const fetchEvents = CountFetchEvents([&] { records = dm.Query<RowFixedRecord>().All(); });

    CHECK(fetchEvents == 3); // 1024 + 1024 + 7
    REQUIRE(records.size() == static_cast<std::size_t>(total));

    // Spot-check the first, a mid-block-boundary, and the last row.
    std::map<int64_t, RowFixedRecord const*> byId;
    for (auto const& r: records)
        byId.emplace(r.id.Value(), &r);
    CHECK(byId[1]->big.Value() == 1);
    CHECK(byId[static_cast<int64_t>(depth)]->big.Value() == static_cast<int64_t>(depth));
    CHECK(byId[static_cast<int64_t>(depth) + 1]->big.Value() == static_cast<int64_t>(depth) + 1);
    CHECK(byId[total]->big.Value() == total);
}

TEST_CASE_METHOD(SqlTestFixture, "RowWiseFetch: empty result set", "[DataMapper][rowwisefetch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<RowFixedRecord>();

    std::vector<RowFixedRecord> records;
    auto const fetchEvents = CountFetchEvents([&] { records = dm.Query<RowFixedRecord>().All(); });

    CHECK(fetchEvents == 0); // SQLFetchScroll immediately returns SQL_NO_DATA
    CHECK(records.empty());
}

TEST_CASE_METHOD(SqlTestFixture, "RowWiseFetch: Where/Range stay correct on the fast path", "[DataMapper][rowwisefetch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<RowFixedRecord>();

    auto seed = std::vector<RowFixedRecord> {};
    for (auto const i: std::views::iota(1, 21))
        seed.push_back({ .id = i, .big = i, .ratio = i * 1.0, .mid = i, .tiny = static_cast<int16_t>(i) });
    dm.CreateAll(seed);

    SECTION("Where + All")
    {
        auto const records = dm.Query<RowFixedRecord>().Where(FieldNameOf<Member(RowFixedRecord::mid)>, ">=", 15).All();
        CHECK(records.size() == 6); // ids 15..20
        for (auto const& r: records)
            CHECK(r.mid.Value() >= 15);
    }

    SECTION("Range")
    {
        auto const records = dm.Query<RowFixedRecord>().OrderBy(FieldNameOf<Member(RowFixedRecord::id)>).Range(5, 4);
        REQUIRE(records.size() == 4);
        CHECK(records.front().id.Value() == 6); // offset 5 -> 6th row (1-based ids)
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RowWiseFetch: inline fixed-capacity strings (with NULL/empty/full)",
                 "[DataMapper][rowwisefetch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<RowFixedStringRecord>();

    auto seed = std::vector<RowFixedStringRecord> {};
    seed.push_back({ .id = 1, .name = SqlAnsiString<32> { "Alice" }, .code = SqlAnsiString<16> { "AAA" }, .number = 10 });
    seed.push_back({ .id = 2, .name = SqlAnsiString<32> {}, .code = std::nullopt, .number = 20 }); // empty name, NULL code
    seed.push_back({ .id = 3,
                     .name = SqlAnsiString<32> { "Charlie Brown the Third Esq." },
                     .code = SqlAnsiString<16> { "0123456789012345" }, // 16 chars: fills capacity exactly
                     .number = 30 });
    dm.CreateAll(seed);

    std::vector<RowFixedStringRecord> records;
    auto const fetchEvents = CountFetchEvents([&] { records = dm.Query<RowFixedStringRecord>().All(); });

    // Where narrow text round-trips byte-exact (MSSQL/SQLite) the fast path runs: one block-fetch.
    // PostgreSQL keeps the per-row (wide) path for fixed strings: one fetch per row.
    if (SqlConnection::RoundTripsNarrowTextByteExact(dm.Connection().ServerType()))
        CHECK(fetchEvents == 1);
    else
        CHECK(fetchEvents == 3);

    REQUIRE(records.size() == 3);
    std::map<int64_t, RowFixedStringRecord const*> byId;
    for (auto const& r: records)
        byId.emplace(r.id.Value(), &r);

    CHECK(byId[1]->name.Value() == "Alice");
    REQUIRE(byId[1]->code.Value().has_value());
    CHECK(byId[1]->code.Value().value() == "AAA");
    CHECK(byId[1]->number.Value() == 10);

    CHECK(byId[2]->name.Value().empty());
    CHECK_FALSE(byId[2]->code.Value().has_value());
    CHECK(byId[2]->number.Value() == 20);

    CHECK(byId[3]->name.Value() == "Charlie Brown the Third Esq.");
    REQUIRE(byId[3]->code.Value().has_value());
    CHECK(byId[3]->code.Value().value() == "0123456789012345");
}

TEST_CASE_METHOD(SqlTestFixture,
                 "RowWiseFetch: record with std::string falls back, still correct",
                 "[DataMapper][rowwisefetch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<RowStringRecord>();

    auto seed = std::vector<RowStringRecord> {};
    seed.push_back({ .id = 1, .name = std::string { "alpha" }, .number = 10 });
    seed.push_back({ .id = 2, .name = std::string { "" }, .number = 20 });
    seed.push_back({ .id = 3, .name = std::string { "a longer value than before" }, .number = 30 });
    dm.CreateAll(seed);

    std::vector<RowStringRecord> records;
    auto const fetchEvents = CountFetchEvents([&] { records = dm.Query<RowStringRecord>().All(); });

    // Not row-wise fetchable: the per-row path runs, so one OnFetchRow per row (3), not one block.
    CHECK(fetchEvents == 3);

    REQUIRE(records.size() == 3);
    std::map<int64_t, RowStringRecord const*> byId;
    for (auto const& r: records)
        byId.emplace(r.id.Value(), &r);
    CHECK(byId[1]->name.Value() == "alpha");
    CHECK(byId[2]->name.Value().empty());
    CHECK(byId[3]->name.Value() == "a longer value than before");
    CHECK(byId[3]->number.Value() == 30);
}

TEST_CASE_METHOD(SqlTestFixture, "RowWiseFetch: statement is reusable after a fast fetch", "[DataMapper][rowwisefetch]")
{
    auto dm = DataMapper {};
    dm.CreateTable<RowFixedRecord>();

    auto seed = std::vector<RowFixedRecord> {};
    for (auto const i: std::views::iota(1, 11))
        seed.push_back({ .id = i, .big = i, .ratio = i * 1.0, .mid = i, .tiny = static_cast<int16_t>(i) });
    dm.CreateAll(seed);

    auto const first = dm.Query<RowFixedRecord>().All();
    CHECK(first.size() == 10);

    // After the row-array attributes were restored, ordinary single-row queries must still work.
    CHECK(dm.Query<RowFixedRecord>().Count() == 10);
    auto const single = dm.QuerySingle<RowFixedRecord>(5);
    REQUIRE(single.has_value());
    CHECK(single->big.Value() == 5);

    // A second fast fetch on a fresh query also works.
    auto const second = dm.Query<RowFixedRecord>().All();
    CHECK(second.size() == 10);
}

// NOLINTEND(bugprone-unchecked-optional-access)
