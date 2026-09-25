// SPDX-License-Identifier: Apache-2.0

// Where a lazy relation load runs (#584, #619). A loader borrows a mapper of its own from the source
// its record was read through - the record's pool, or its mapper's connection string - for the
// duration of one load, instead of running on a thread-local mapper built from the default
// connection string and sharing that mapper's one statement with every other load on the thread.

#include "../Utils.hpp"

#include <Lightweight/DataMapper/Pool.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

using namespace Lightweight;
using namespace std::string_view_literals;

struct RoutingOwner
{
    Field<int64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> name;
};

struct RoutingItem
{
    Field<int64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> label;
    BelongsTo<Member(RoutingOwner::id), SqlRealName { "owner_id" }> owner;
};

struct RoutingChild;

struct RoutingParent
{
    Field<int64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> name;
    HasMany<RoutingChild> children;
};

struct RoutingChild
{
    Field<int64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlAnsiString<30>> label;
    BelongsTo<Member(RoutingParent::id), SqlRealName { "parent_id" }> parent;
};

namespace
{
constexpr auto SingleConnectionPoolConfig = PoolConfig {
    .initialSize = 1,
    .maxSize = 1,
    .growthStrategy = GrowthStrategy::BoundedWait,
};

constexpr auto OverflowOfTwoConfig = PoolConfig {
    .initialSize = 0,
    .maxSize = 2,
    .growthStrategy = GrowthStrategy::BoundedOverflow,
};

// Seeds one owner and one item referencing it, returning the item's id.
int64_t SeedOwnerAndItem(DataMapper& dm, std::string_view ownerName)
{
    dm.CreateTables<RoutingOwner, RoutingItem>();
    auto owner = RoutingOwner { .id = {}, .name = SqlAnsiString<30> { ownerName } };
    dm.Create(owner);
    auto item = RoutingItem { .id = {}, .label = "item", .owner = owner };
    dm.Create(item);
    return item.id.Value();
}
} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "Relation loading: a record read through a second database loads its relations from there",
                 "[DataMapper][BelongsTo]")
{
    auto dmDefault = DataMapper {};
    if (dmDefault.Connection().ServerType() != SqlServerType::SQLITE)
        SKIP("needs a second, independent database; trivially a second SQLite file");

    std::ignore = SeedOwnerAndItem(dmDefault, "from-default");

    auto const otherFile = std::string { "relation-routing-other.db" };
    std::filesystem::remove(otherFile);
    // Not ParseConnectionString/BuildConnectionString: the latter braces every value, which the SQLite
    // ODBC driver keeps as part of the file name.
    auto dmOther = DataMapper { SqlConnectionString { std::regex_replace(SqlConnection::DefaultConnectionString().value,
                                                                         std::regex { "Database=[^;]*", std::regex::icase },
                                                                         "Database=" + otherFile) } };
    auto const itemId = SeedOwnerAndItem(dmOther, "from-other");

    auto item = dmOther.QuerySingle<RoutingItem>(itemId);
    REQUIRE(item.has_value());
    if (!item.has_value())
        return;
    auto again = *item; // a second record with its relation still unloaded, for after the switch below
    CHECK(item->owner->name.Value() == "from-other");

    // A mapper with a connection string of its own does not follow the default, so switching the
    // default does not concern its records.
    auto const previous = SqlConnectionString { .value = SqlConnection::DefaultConnectionString().value };
    auto const restore = detail::Finally([&] { SqlConnection::SetDefaultConnectionString(previous); });
    SqlConnection::SetDefaultConnectionString(SqlConnectionString { previous.value + ";" });
    CHECK(again.owner->name.Value() == "from-other");
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Relation loading: a pooled record borrows its load connection from the pool",
                 "[DataMapper][BelongsTo][Pool]")
{
    auto dm = DataMapper {};
    auto const itemId = SeedOwnerAndItem(dm, "owner");

    auto pool = Pool<OverflowOfTwoConfig> {};
    auto item = pool.Acquire()->QuerySingle<RoutingItem>(itemId); // its connection goes back to the pool
    REQUIRE(item.has_value());
    if (!item.has_value())
        return;

    // Take that connection out again, so the pool is empty when the relation is touched: a load that
    // borrows from this pool has to make the pool's second connection, and leaves it idle there. Any
    // other connection a load could run on - a thread-local, a one-off - leaves the pool empty.
    auto const held = pool.Acquire();
    REQUIRE(pool.IdleCount() == 0);
    CHECK(item->owner->name.Value() == "owner");
    CHECK(pool.IdleCount() == 1);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Relation loading: a load does not wait for a pool its caller keeps exhausted",
                 "[DataMapper][BelongsTo][Pool]")
{
    auto dm = DataMapper {};
    auto const itemId = SeedOwnerAndItem(dm, "owner");

    auto pool = Pool<SingleConnectionPoolConfig> {};
    auto const held = pool.Acquire(); // the pool's only connection, kept for the whole test
    auto item = held->QuerySingle<RoutingItem>(itemId);
    REQUIRE(item.has_value());
    if (!item.has_value())
        return;

    // Waiting for a pooled connection here would wait for `held` - forever. The load runs on a
    // one-off connection instead.
    CHECK(item->owner->name.Value() == "owner");
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Relation loading: a load inside HasMany::Each runs on a connection of its own",
                 "[DataMapper][HasMany]")
{
    auto dm = DataMapper {};
    dm.CreateTables<RoutingParent, RoutingChild>();
    auto parent = RoutingParent { .id = {}, .name = "parent", .children = {} };
    dm.Create(parent);
    for (auto const label: { "a"sv, "b"sv, "c"sv })
    {
        auto child = RoutingChild { .id = {}, .label = SqlAnsiString<30> { label }, .parent = parent };
        dm.Create(child);
    }

    auto loaded = dm.QuerySingle<RoutingParent>(parent.id.Value());
    REQUIRE(loaded.has_value());
    if (!loaded.has_value())
        return;

    // Each() keeps its cursor open while calling back. A relation touched from the callback used to
    // run on that same connection - which SQL Server rejects without MARS ("Connection is busy with
    // results for another command") - and to be installed before the row was fetched, so it loaded
    // with a default-constructed key.
    SECTION("BelongsTo of each child")
    {
        auto parentNames = std::vector<std::string> {};
        loaded->children.Each(
            [&](RoutingChild const& child) { parentNames.emplace_back(std::string { child.parent->name.Value() }); });
        CHECK(parentNames == std::vector<std::string> { "parent", "parent", "parent" });
    }

    SECTION("HasMany::Count of the same relation")
    {
        auto counts = std::vector<size_t> {};
        loaded->children.Each([&](RoutingChild const& /*child*/) {
            auto again = dm.QuerySingle<RoutingParent>(parent.id.Value());
            REQUIRE(again.has_value());
            if (!again.has_value())
                return;
            counts.push_back(again->children.Count());
        });
        CHECK(counts == std::vector<size_t> { 3, 3, 3 });
    }
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Relation loading: a record read before the default connection string changed does not load",
                 "[DataMapper][BelongsTo][Pool]")
{
    auto dm = DataMapper {};
    auto const itemId = SeedOwnerAndItem(dm, "owner");

    auto pool = Pool<SingleConnectionPoolConfig> {};
    auto fromPlain = dm.QuerySingle<RoutingItem>(itemId);
    auto fromPool = pool.Acquire()->QuerySingle<RoutingItem>(itemId);
    REQUIRE(fromPlain.has_value());
    REQUIRE(fromPool.has_value());
    if (!fromPlain.has_value() || !fromPool.has_value())
        return;
    auto const fromPlainAfterReturn = *fromPlain; // copies with their relations still unloaded
    auto const fromPoolAfterReturn = *fromPool;

    // A different string for the same database: the switch is what matters, not where it points. The
    // loads below fail before connecting anywhere.
    auto const previous = SqlConnectionString { .value = SqlConnection::DefaultConnectionString().value };
    auto const restore = detail::Finally([&] { SqlConnection::SetDefaultConnectionString(previous); });
    SqlConnection::SetDefaultConnectionString(SqlConnectionString { previous.value + ";" });

    SECTION("a record read through a plain mapper")
    {
        CHECK_THROWS_AS(std::ignore = fromPlain->owner->name, SqlDefaultConnectionChangedError);
    }
    SECTION("a record read through a pooled mapper")
    {
        CHECK_THROWS_AS(std::ignore = fromPool->owner->name, SqlDefaultConnectionChangedError);
    }
    SECTION("switching back to the string the records were read with loads again")
    {
        SqlConnection::SetDefaultConnectionString(previous);
        CHECK(fromPlainAfterReturn.owner->name.Value() == "owner");
        CHECK(fromPoolAfterReturn.owner->name.Value() == "owner");
    }
}
