// SPDX-License-Identifier: Apache-2.0

// clang-format off
#include "../Utils.hpp" // must precede the async test utilities, which rely on the fixture headers
#include "../Async/AsyncTestUtils.hpp"
// clang-format on

#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/SyncWait.hpp>
#include <Lightweight/Async/ThreadPoolExecutor.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;
using namespace std::string_view_literals;
using namespace Lightweight;

struct TimestampArticle
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id {};
    Field<SqlAnsiString<40>> title {};
    Field<SqlDateTime, FieldTimestamp::CreatedAt> createdAt {};
    Field<SqlDateTime, FieldTimestamp::UpdatedAt, SqlRealName { "updated_at" }> updatedAt {};

    static constexpr std::string_view TableName = "TimestampArticles"sv;
};

/// Optional timestamps, with the marker given in the second option slot.
struct TimestampNote
{
    Field<SqlGuid, PrimaryKey::AutoAssign> id {};
    Field<SqlAnsiString<40>> body {};
    Field<std::optional<SqlDateTime>, SqlRealName { "created_at" }, FieldTimestamp::CreatedAt> createdAt {};
    Field<std::optional<SqlDateTime>, FieldTimestamp::UpdatedAt> updatedAt {};

    static constexpr std::string_view TableName = "TimestampNotes"sv;
};

namespace
{

// The marker resolves from either option slot, alone or beside a column-name override.
using CreatedAtField = Field<SqlDateTime, FieldTimestamp::CreatedAt>;
using RenamedUpdatedAtField = Field<SqlDateTime, FieldTimestamp::UpdatedAt, SqlRealName { "updated_at" }>;
using RenamedFirstCreatedAtField =
    Field<std::optional<SqlDateTime>, SqlRealName { "created_at" }, FieldTimestamp::CreatedAt>;

static_assert(CreatedAtField::TimestampKind == FieldTimestamp::CreatedAt);
static_assert(CreatedAtField::IsCreatedAtTimestamp && !CreatedAtField::IsUpdatedAtTimestamp);
static_assert(CreatedAtField::IsAutoTimestamp && !CreatedAtField::IsPrimaryKey);
static_assert(RenamedUpdatedAtField::IsUpdatedAtTimestamp && RenamedUpdatedAtField::ColumnNameOverride == "updated_at");
static_assert(RenamedFirstCreatedAtField::IsCreatedAtTimestamp
              && RenamedFirstCreatedAtField::ColumnNameOverride == "created_at");
static_assert(Field<SqlDateTime>::TimestampKind == FieldTimestamp::None && !Field<SqlDateTime>::IsAutoTimestamp);
static_assert(Field<SqlGuid, PrimaryKey::AutoAssign>::TimestampKind == FieldTimestamp::None);
static_assert(FieldTimestampOf<RenamedUpdatedAtField> == FieldTimestamp::UpdatedAt);
static_assert(FieldTimestampOf<int> == FieldTimestamp::None);
static_assert(IsAutoTimestampField<CreatedAtField const&>);
static_assert(!IsAutoTimestampField<Field<SqlDateTime>>);
static_assert(FieldNameOf<Member(TimestampArticle::updatedAt)> == "updated_at");

constexpr auto CreationTime = SqlDateTime { 2024y, std::chrono::March, 5d, 10h, 20min, 30s };
constexpr auto UpdateTime = SqlDateTime { 2024y, std::chrono::April, 6d, 11h, 21min, 31s };
constexpr auto HistoricalTime = SqlDateTime { 2001y, std::chrono::January, 2d, 3h, 4min, 5s };

/// Reloads the @p Record with primary key @p id, failing the test if it is not there.
template <typename Record>
Record Reload(DataMapper& dm, SqlGuid const& id)
{
    auto reloaded = dm.QuerySingle<Record>(id);
    REQUIRE(reloaded.has_value());
    if (!reloaded.has_value())
        throw std::logic_error { "Reload: record not found" };
    return std::move(*reloaded);
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: Create sets created_at and updated_at", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    auto clockReads = 0;
    dm.SetTimestampClock([&clockReads] {
        ++clockReads;
        return CreationTime;
    });
    dm.CreateTable<TimestampArticle>();

    auto article = TimestampArticle { .title = "first" };
    dm.Create(article);

    // Written back into the record, from a single read of the clock.
    CHECK(article.createdAt.Value() == CreationTime);
    CHECK(article.updatedAt.Value() == CreationTime);
    CHECK(clockReads == 1);
    CHECK_FALSE(dm.IsModified(article));

    auto const stored = Reload<TimestampArticle>(dm, article.id.Value());
    CHECK(stored.createdAt.Value() == CreationTime);
    CHECK(stored.updatedAt.Value() == CreationTime);
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: an explicit created_at survives Create", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    dm.SetTimestampClock([] { return CreationTime; });
    dm.CreateTable<TimestampArticle>();

    auto article = TimestampArticle { .title = "imported", .createdAt = HistoricalTime };
    dm.Create(article);

    CHECK(article.createdAt.Value() == HistoricalTime);
    CHECK(article.updatedAt.Value() == CreationTime);

    auto const stored = Reload<TimestampArticle>(dm, article.id.Value());
    CHECK(stored.createdAt.Value() == HistoricalTime);
    CHECK(stored.updatedAt.Value() == CreationTime);
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: Update bumps updated_at only", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    auto now = CreationTime;
    dm.SetTimestampClock([&now] { return now; });
    dm.CreateTable<TimestampArticle>();

    auto article = TimestampArticle { .title = "draft" };
    dm.Create(article);

    now = UpdateTime;
    article.title = "final";
    dm.Update(article);

    CHECK(article.createdAt.Value() == CreationTime);
    CHECK(article.updatedAt.Value() == UpdateTime);
    CHECK_FALSE(dm.IsModified(article));

    auto const stored = Reload<TimestampArticle>(dm, article.id.Value());
    CHECK(stored.title.Value() == "final");
    CHECK(stored.createdAt.Value() == CreationTime);
    CHECK(stored.updatedAt.Value() == UpdateTime);

    SECTION("on a reloaded record as well")
    {
        auto reloaded = Reload<TimestampArticle>(dm, article.id.Value());
        now = HistoricalTime;
        reloaded.title = "again";
        dm.Update(reloaded);

        auto const restored = Reload<TimestampArticle>(dm, article.id.Value());
        CHECK(restored.createdAt.Value() == CreationTime);
        CHECK(restored.updatedAt.Value() == HistoricalTime);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: a no-op Update stays a no-op", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    auto clockReads = 0;
    dm.SetTimestampClock([&clockReads] {
        ++clockReads;
        return clockReads == 1 ? CreationTime : UpdateTime;
    });
    dm.CreateTable<TimestampArticle>();

    auto article = TimestampArticle { .title = "untouched" };
    dm.Create(article);
    REQUIRE(clockReads == 1);

    auto const statisticsEnabled = ScopedSqlStatisticsEnabled {};
    auto const before = SqlStatistics::Instance().Snapshot();
    dm.Update(article);
    auto const after = SqlStatistics::Instance().Snapshot();

    CHECK(after[SqlStatisticsOperation::Prepare].Total() == before[SqlStatisticsOperation::Prepare].Total());
    CHECK(after[SqlStatisticsOperation::Execute].Total() == before[SqlStatisticsOperation::Execute].Total());
    CHECK(clockReads == 1);
    CHECK(article.updatedAt.Value() == CreationTime);

    auto const stored = Reload<TimestampArticle>(dm, article.id.Value());
    CHECK(stored.updatedAt.Value() == CreationTime);
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: an explicit updated_at survives Update", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    auto now = CreationTime;
    dm.SetTimestampClock([&now] { return now; });
    dm.CreateTable<TimestampArticle>();

    auto article = TimestampArticle { .title = "draft" };
    dm.Create(article);
    now = UpdateTime;

    SECTION("alongside another change")
    {
        article.title = "final";
        article.updatedAt = HistoricalTime;
        dm.Update(article);
    }

    SECTION("as the only change")
    {
        article.updatedAt = HistoricalTime;
        dm.Update(article);
    }

    CHECK(article.updatedAt.Value() == HistoricalTime);
    auto const stored = Reload<TimestampArticle>(dm, article.id.Value());
    CHECK(stored.createdAt.Value() == CreationTime);
    CHECK(stored.updatedAt.Value() == HistoricalTime);
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: CreateExplicit binds them", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    dm.SetTimestampClock([] { return CreationTime; });
    dm.CreateTable<TimestampArticle>();

    auto const fresh = TimestampArticle { .id = SqlGuid::Create(), .title = "fresh" };
    auto imported = TimestampArticle { .id = SqlGuid::Create(), .title = "imported", .createdAt = HistoricalTime };
    imported.updatedAt = HistoricalTime;
    dm.CreateExplicit(fresh);
    dm.CreateExplicit(imported);

    // The records are const inputs: nothing is written back.
    CHECK(detail::IsUnsetTimestamp(fresh.createdAt.Value()));
    CHECK(detail::IsUnsetTimestamp(fresh.updatedAt.Value()));

    auto const storedFresh = Reload<TimestampArticle>(dm, fresh.id.Value());
    CHECK(storedFresh.createdAt.Value() == CreationTime);
    CHECK(storedFresh.updatedAt.Value() == CreationTime);

    auto const storedImported = Reload<TimestampArticle>(dm, imported.id.Value());
    CHECK(storedImported.createdAt.Value() == HistoricalTime);
    CHECK(storedImported.updatedAt.Value() == HistoricalTime);
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: CreateCopyOf stamps the copy afresh", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    auto now = CreationTime;
    dm.SetTimestampClock([&now] { return now; });
    dm.CreateTable<TimestampArticle>();

    auto original = TimestampArticle { .title = "original" };
    dm.Create(original);

    now = UpdateTime;
    auto const copyId = dm.CreateCopyOf(original);

    auto const copy = Reload<TimestampArticle>(dm, copyId);
    CHECK(copy.title.Value() == "original");
    CHECK(copy.createdAt.Value() == UpdateTime);
    CHECK(copy.updatedAt.Value() == UpdateTime);
    CHECK(original.createdAt.Value() == CreationTime);
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: CreateAll binds them", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    auto clockReads = 0;
    dm.SetTimestampClock([&clockReads] {
        ++clockReads;
        return CreationTime;
    });
    dm.CreateTable<TimestampArticle>();

    auto const records = std::array {
        TimestampArticle { .id = SqlGuid::Create(), .title = "a" },
        TimestampArticle { .id = SqlGuid::Create(), .title = "b", .createdAt = HistoricalTime },
        TimestampArticle { .id = SqlGuid::Create(), .title = "c" },
    };
    dm.CreateAll(records);

    CHECK(clockReads == 1);
    CHECK(detail::IsUnsetTimestamp(records[0].createdAt.Value()));

    auto const storedA = Reload<TimestampArticle>(dm, records[0].id.Value());
    CHECK(storedA.createdAt.Value() == CreationTime);
    CHECK(storedA.updatedAt.Value() == CreationTime);

    auto const storedB = Reload<TimestampArticle>(dm, records[1].id.Value());
    CHECK(storedB.createdAt.Value() == HistoricalTime);
    CHECK(storedB.updatedAt.Value() == CreationTime);

    auto const storedC = Reload<TimestampArticle>(dm, records[2].id.Value());
    CHECK(storedC.createdAt.Value() == CreationTime);
    CHECK(storedC.updatedAt.Value() == CreationTime);
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: UpdateAll bumps updated_at", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    auto now = CreationTime;
    dm.SetTimestampClock([&now] { return now; });
    dm.CreateTable<TimestampArticle>();

    auto records = std::vector<TimestampArticle> {
        TimestampArticle { .title = "a" },
        TimestampArticle { .title = "b" },
    };
    for (auto& record: records)
        dm.Create(record);

    now = UpdateTime;
    for (auto& record: records)
        record.title = std::format("{}!", record.title.Value());
    dm.UpdateAll(records);

    // The records are const inputs: nothing is written back.
    CHECK(records[0].updatedAt.Value() == CreationTime);

    for (auto const& record: records)
    {
        auto const stored = Reload<TimestampArticle>(dm, record.id.Value());
        CHECK(stored.title.Value() == record.title.Value());
        CHECK(stored.createdAt.Value() == CreationTime);
        CHECK(stored.updatedAt.Value() == UpdateTime);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: the column-name override applies", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    dm.SetTimestampClock([] { return CreationTime; });
    dm.CreateTable<TimestampArticle>();

    auto article = TimestampArticle { .title = "renamed" };
    dm.Create(article);

    auto const updatedAt =
        dm.Execute<SqlDateTime>(std::format(R"(SELECT "updated_at" FROM "{}")", RecordTableName<TimestampArticle>));
    REQUIRE(updatedAt.has_value());
    if (updatedAt.has_value())
        CHECK(*updatedAt == CreationTime);
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: optional timestamp fields", "[DataMapper][Timestamp]")
{
    auto dm = DataMapper();
    auto now = CreationTime;
    dm.SetTimestampClock([&now] { return now; });
    dm.CreateTable<TimestampNote>();

    auto note = TimestampNote { .body = "note" };
    dm.Create(note);
    CHECK(note.createdAt.ValueOr(SqlDateTime {}) == CreationTime);
    CHECK(note.updatedAt.ValueOr(SqlDateTime {}) == CreationTime);

    now = UpdateTime;
    note.body = "edited";
    dm.Update(note);
    CHECK(note.createdAt.ValueOr(SqlDateTime {}) == CreationTime);
    CHECK(note.updatedAt.ValueOr(SqlDateTime {}) == UpdateTime);

    auto const stored = Reload<TimestampNote>(dm, note.id.Value());
    CHECK(stored.createdAt.ValueOr(SqlDateTime {}) == CreationTime);
    CHECK(stored.updatedAt.ValueOr(SqlDateTime {}) == UpdateTime);

    auto const createdAt =
        dm.Execute<SqlDateTime>(std::format(R"(SELECT "created_at" FROM "{}")", RecordTableName<TimestampNote>));
    REQUIRE(createdAt.has_value());
    if (createdAt.has_value())
        CHECK(*createdAt == CreationTime);

    auto imported = TimestampNote { .body = "imported", .createdAt = HistoricalTime };
    dm.Create(imported);
    CHECK(Reload<TimestampNote>(dm, imported.id.Value()).createdAt.ValueOr(SqlDateTime {}) == HistoricalTime);
}

TEST_CASE_METHOD(SqlTestFixture, "DataMapper timestamps: CreateAsync and UpdateAsync", "[DataMapper][Timestamp][Async]")
{
    Async::ThreadPoolExecutor dbWorkers { 1 };
    Async::ManualExecutor appLoop;

    auto dm = DataMapper();
    dm.Connection().EnableAsync(dbWorkers, appLoop);
    auto now = CreationTime;
    dm.SetTimestampClock([&now] { return now; });
    dm.CreateTable<TimestampArticle>();

    auto article = TimestampArticle { .title = "async" };
    std::ignore = Async::SyncWaitPumping(dm.CreateAsync(article), appLoop);
    CHECK(article.createdAt.Value() == CreationTime);
    CHECK(article.updatedAt.Value() == CreationTime);

    now = UpdateTime;
    article.title = "async!";
    Async::SyncWaitPumping(dm.UpdateAsync(article), appLoop);
    CHECK(article.createdAt.Value() == CreationTime);
    CHECK(article.updatedAt.Value() == UpdateTime);

    auto const stored =
        RequireValue(Async::SyncWaitPumping(dm.QuerySingleAsync<TimestampArticle>(article.id.Value()), appLoop));
    CHECK(stored.createdAt.Value() == CreationTime);
    CHECK(stored.updatedAt.Value() == UpdateTime);
}
