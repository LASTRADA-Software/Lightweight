// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using namespace Lightweight;
using namespace std::string_view_literals;

namespace
{

// Counts SqlStatement::Prepare() requests, so a test can relate the number of logical prepares to the
// number of driver round-trips the cache statistics report.
struct PrepareProbe: SqlLogger::Null
{
    std::size_t prepares = 0;

    void OnPrepare(std::string_view const& /*query*/) override
    {
        ++prepares;
    }
};

// RAII swap of the active SqlLogger (restores the previous one on scope exit).
struct LoggerSwap
{
    SqlLogger* previous;
    explicit LoggerSwap(SqlLogger& replacement):
        previous { &SqlLogger::GetLogger() }
    {
        SqlLogger::SetLogger(replacement);
    }
    ~LoggerSwap()
    {
        SqlLogger::SetLogger(*previous);
    }
    LoggerSwap(LoggerSwap const&) = delete;
    LoggerSwap& operator=(LoggerSwap const&) = delete;
    LoggerSwap(LoggerSwap&&) = delete;
    LoggerSwap& operator=(LoggerSwap&&) = delete;
};

// Creates the table the query-executing tests below read from, and returns a connection whose
// prepared-statement cache is enabled with freshly zeroed statistics. Table creation runs *before* the
// cache is enabled, so the DDL-triggered cache invalidation does not perturb the counters.
SqlConnection MakeSeededConnection(std::size_t capacity = 8)
{
    using namespace Lightweight::SqlColumnTypeDefinitions;

    auto connection = SqlConnection {};
    {
        auto stmt = SqlStatement { connection };
        stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
            migration.DropTableIfExists("stmt_cache");
            migration.CreateTable("stmt_cache").PrimaryKey("id", Integer {}).RequiredColumn("value", Integer {});
        });

        stmt.Prepare("INSERT INTO stmt_cache (id, value) VALUES (?, ?)");
        for (auto const id: { 1, 2, 3 })
            std::ignore = stmt.Execute(id, id * 10);
    }

    connection.SetPreparedStatementCacheCapacity(capacity);
    connection.PreparedStatementCache().ResetStatistics();
    return connection;
}

// Reads the single `value` of the row with the given id through a prepared (and thus cacheable) query.
int SelectValue(SqlStatement& stmt, int id)
{
    stmt.Prepare("SELECT value FROM stmt_cache WHERE id = ?");
    auto cursor = stmt.Execute(id);
    REQUIRE(cursor.FetchRow());
    return cursor.GetColumn<int>(1);
}

} // namespace

// A minimal record whose Create() re-prepares one and the same INSERT statement.
struct CachedThing
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> name;
};

TEST_CASE_METHOD(SqlTestFixture, "PreparedStatementCache: disabled by default", "[SqlPreparedStatementCache]")
{
    auto connection = SqlConnection {};
    CHECK(connection.PreparedStatementCacheCapacity() == PreparedStatementCacheCapacityDefault);
    CHECK(connection.PreparedStatementCacheCapacity() == 0);

    auto stmt = SqlStatement { connection };
    for (auto i = 0; i < 3; ++i)
        stmt.Prepare("SELECT 1");

    auto const& stats = connection.PreparedStatementCache().Stats();
    CHECK(stats.hits == 0);
    CHECK(stats.misses == 0);
    CHECK(connection.PreparedStatementCache().Size() == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "PreparedStatementCache: repeated prepare hits the cache", "[SqlPreparedStatementCache]")
{
    auto connection = MakeSeededConnection();
    auto probe = PrepareProbe {};
    auto const loggerSwap = LoggerSwap { probe };

    auto stmt = SqlStatement { connection };
    for (auto i = 0; i < 5; ++i)
        CHECK(SelectValue(stmt, 2) == 20);

    auto const& stats = connection.PreparedStatementCache().Stats();
    CHECK(probe.prepares == 5); // five logical prepares ...
    CHECK(stats.misses == 1);   // ... but only the first one reached the driver
    CHECK(stats.hits == 4);
    CHECK(stats.evictions == 0);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "PreparedStatementCache: a second statement reuses a pooled handle",
                 "[SqlPreparedStatementCache]")
{
    auto connection = MakeSeededConnection();

    {
        auto first = SqlStatement { connection };
        CHECK(SelectValue(first, 1) == 10);
    }

    // The destroyed statement handed its prepared handle to the pool rather than freeing it.
    CHECK(connection.PreparedStatementCache().Size() == 1);
    CHECK(connection.PreparedStatementCache().Stats().misses == 1);

    {
        auto second = SqlStatement { connection };
        CHECK(SelectValue(second, 3) == 30);
        CHECK(connection.PreparedStatementCache().Stats().hits == 1);
        CHECK(connection.PreparedStatementCache().Size() == 0); // checked out while in use
    }

    CHECK(connection.PreparedStatementCache().Size() == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "PreparedStatementCache: concurrent statements each get their own handle",
                 "[SqlPreparedStatementCache]")
{
    auto connection = MakeSeededConnection();

    auto first = SqlStatement { connection };
    auto second = SqlStatement { connection };

    CHECK(SelectValue(first, 1) == 10);
    CHECK(SelectValue(second, 2) == 20); // same query text, but `first` still holds its handle

    auto const& stats = connection.PreparedStatementCache().Stats();
    CHECK(stats.misses == 2);
    CHECK(stats.hits == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "PreparedStatementCache: bounded LRU evicts", "[SqlPreparedStatementCache]")
{
    auto connection = MakeSeededConnection(1);
    REQUIRE(connection.PreparedStatementCacheCapacity() == 1);

    auto stmt = SqlStatement { connection };
    stmt.Prepare("SELECT value FROM stmt_cache WHERE id = 1");
    stmt.Prepare("SELECT value FROM stmt_cache WHERE id = 2");
    stmt.Prepare("SELECT value FROM stmt_cache WHERE id = 3");

    auto const& stats = connection.PreparedStatementCache().Stats();
    CHECK(stats.misses == 3);
    CHECK(stats.hits == 0);
    CHECK(stats.evictions == 1);
    CHECK(connection.PreparedStatementCache().Size() <= 1);
}

TEST_CASE_METHOD(SqlTestFixture, "PreparedStatementCache: a statement can opt out", "[SqlPreparedStatementCache]")
{
    auto connection = MakeSeededConnection();

    auto stmt = SqlStatement { connection };
    stmt.SetPreparedStatementCaching(SqlPreparedStatementCaching::Disabled);
    CHECK(stmt.PreparedStatementCaching() == SqlPreparedStatementCaching::Disabled);

    for (auto i = 0; i < 3; ++i)
        CHECK(SelectValue(stmt, 1) == 10);

    auto const& stats = connection.PreparedStatementCache().Stats();
    CHECK(stats.hits == 0);
    CHECK(stats.misses == 0);
    CHECK(connection.PreparedStatementCache().Size() == 0);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "PreparedStatementCache: a hand-bound statement pools the real parameter count",
                 "[SqlPreparedStatementCache]")
{
    auto connection = MakeSeededConnection();

    {
        // BindInputParameter() replaces the expected parameter count with the "unknown" sentinel. That
        // sentinel must not reach the pool: a later cache hit adopts the pooled count and skips
        // SQLNumParams, and Execute() would then reject a perfectly valid argument list.
        auto stmt = SqlStatement { connection };
        stmt.Prepare("SELECT value FROM stmt_cache WHERE id = ?");
        auto const id = 1;
        stmt.BindInputParameter(1, id);
        auto cursor = stmt.Execute();
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == 10);
    }

    REQUIRE(connection.PreparedStatementCache().Size() == 1);

    auto stmt = SqlStatement { connection };
    CHECK(SelectValue(stmt, 2) == 20); // reuses the pooled handle, passing its argument through Execute()
    CHECK(connection.PreparedStatementCache().Stats().hits == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "PreparedStatementCache: reused handles keep returning correct results",
                 "[SqlPreparedStatementCache]")
{
    auto cached = std::vector<int> {};
    auto uncached = std::vector<int> {};

    {
        auto connection = MakeSeededConnection();
        auto stmt = SqlStatement { connection };
        for (auto i = 0; i < 4; ++i)
            for (auto const id: { 1, 2, 3 })
                cached.push_back(SelectValue(stmt, id));
        CHECK(connection.PreparedStatementCache().Stats().hits > 0);
    }

    {
        auto connection = SqlConnection {};
        auto stmt = SqlStatement { connection };
        for (auto i = 0; i < 4; ++i)
            for (auto const id: { 1, 2, 3 })
                uncached.push_back(SelectValue(stmt, id));
        CHECK(connection.PreparedStatementCache().Stats().hits == 0);
    }

    CHECK(cached == uncached);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "PreparedStatementCache: pooled handles survive interleaved query texts",
                 "[SqlPreparedStatementCache]")
{
    auto connection = MakeSeededConnection();
    auto stmt = SqlStatement { connection };

    for (auto i = 0; i < 3; ++i)
    {
        CHECK(SelectValue(stmt, 1) == 10);

        stmt.Prepare("SELECT COUNT(*) FROM stmt_cache");
        auto cursor = stmt.Execute();
        REQUIRE(cursor.FetchRow());
        CHECK(cursor.GetColumn<int>(1) == 3);
    }

    auto const& stats = connection.PreparedStatementCache().Stats();
    CHECK(stats.misses == 2); // one per distinct query text
    CHECK(stats.hits == 4);
    CHECK(stats.evictions == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "PreparedStatementCache: MigrateDirect drops cached plans", "[SqlPreparedStatementCache]")
{
    using namespace Lightweight::SqlColumnTypeDefinitions;

    auto connection = MakeSeededConnection();
    {
        auto stmt = SqlStatement { connection };
        CHECK(SelectValue(stmt, 1) == 10);
    }
    REQUIRE(connection.PreparedStatementCache().Size() == 1);

    auto stmt = SqlStatement { connection };
    stmt.MigrateDirect([](SqlMigrationQueryBuilder& migration) {
        migration.DropTableIfExists("stmt_cache_other");
        migration.CreateTable("stmt_cache_other").PrimaryKey("id", Integer {});
    });

    CHECK(connection.PreparedStatementCache().Size() == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "PreparedStatementCache: shrinking the capacity evicts", "[SqlPreparedStatementCache]")
{
    auto connection = MakeSeededConnection();

    {
        auto stmt = SqlStatement { connection };
        stmt.Prepare("SELECT value FROM stmt_cache WHERE id = 1");
        stmt.Prepare("SELECT value FROM stmt_cache WHERE id = 2");
    }
    REQUIRE(connection.PreparedStatementCache().Size() == 2);

    connection.SetPreparedStatementCacheCapacity(0);
    CHECK(connection.PreparedStatementCache().Size() == 0);
    CHECK(connection.PreparedStatementCacheCapacity() == 0);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "PreparedStatementCache: DataMapper benefits without call-site changes",
                 "[SqlPreparedStatementCache][DataMapper]")
{
    auto dm = DataMapper {};
    dm.CreateTable<CachedThing>();

    dm.Connection().SetPreparedStatementCacheCapacity(PreparedStatementCacheCapacitySuggested);
    dm.Connection().PreparedStatementCache().ResetStatistics();

    for (auto i = 0; i < 5; ++i)
    {
        auto thing = CachedThing {};
        thing.name = std::format("Thing {}", i);
        dm.Create(thing);
    }

    // Five DataMapper::Create() calls prepare the identical INSERT: only the first reached the driver.
    auto const& stats = dm.Connection().PreparedStatementCache().Stats();
    CHECK(stats.misses == 1);
    CHECK(stats.hits == 4);

    CHECK(dm.Query<CachedThing>().All().size() == 5);
}
