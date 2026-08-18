// SPDX-License-Identifier: Apache-2.0
//
// Runtime benchmark for relation loading (issue #563).
//
// Unlike `benchmark.cpp`, which measures *compile* time, this one measures wall-clock time and the
// number of statements issued, comparing the on-demand relation loaders against `With<>()` eager
// loading over the same data. It needs a live database: pass an ODBC connection string as the third
// argument to run it against something other than the default local SQLite file.
//
//     LightweightRelationBenchmark [owners] [childrenPerOwner] [connectionString]
//
// The statement counts come from a SqlLogger subclass rather than from an estimate, so a change that
// silently reintroduces an N+1 shows up as a count, not just as a slower number.

#include <Lightweight/Lightweight.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace Lightweight;
using namespace std::chrono;

#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
    #define Member(x) ^^x
#else
    #define Member(x) &x
#endif

struct Child;

struct Owner
{
    static constexpr std::string_view TableName = "BenchOwner";
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<40>> name {};
    HasMany<Child> children {};
};

struct Child
{
    static constexpr std::string_view TableName = "BenchChild";
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<40>> label {};
    BelongsTo<Member(Owner::id), SqlRealName { "owner_id" }> owner {};
};

// Counts prepares/executes so the query count per strategy is reported, not guessed.
class CountingLogger final: public SqlLogger::Null
{
  public:
    size_t prepares = 0;
    size_t executes = 0;
    size_t directs = 0;
    void OnPrepare(std::string_view const&) override
    {
        ++prepares;
    }
    void OnExecute(std::string_view const&) override
    {
        ++executes;
    }
    void OnExecuteDirect(std::string_view const&) override
    {
        ++directs;
    }
    void Reset()
    {
        prepares = executes = directs = 0;
    }
};

namespace
{
CountingLogger g_logger;

size_t g_repetitions = 3;

/// Runs @p f g_repetitions times and returns the fastest wall-clock time, so a cold connection,
/// a cold page cache or a one-off server-side plan compilation does not get reported as the cost
/// of the strategy under test.
template <typename F>
double TimeMs(F&& f)
{
    double best = 1e18;
    for (size_t i = 0; i < g_repetitions; ++i)
    {
        auto const start = steady_clock::now();
        f();
        best = std::min(best, duration<double, std::milli>(steady_clock::now() - start).count());
        if (i + 1 < g_repetitions)
            g_logger.Reset();
    }
    return best;
}

void Report(char const* name, double ms, size_t rows)
{
    std::printf(
        "%-46s %9.2f ms   prepare=%-6zu execute=%-6zu rows=%zu\n", name, ms, g_logger.prepares, g_logger.executes, rows);
    g_logger.Reset();
}
} // namespace

int main(int argc, char** argv)
{
    size_t const owners = argc > 1 ? std::stoul(argv[1]) : 500;
    size_t const childrenPerOwner = argc > 2 ? std::stoul(argv[2]) : 10;
    std::string const connectionString = argc > 3 ? argv[3] : std::string { "DRIVER=SQLite3;Database=/tmp/lw-bench.sqlite" };

    SqlConnection::SetDefaultConnectionString(SqlConnectionString { connectionString });
    SqlLogger::SetLogger(g_logger);

    auto dm = DataMapper {};
    std::ignore = SqlStatement { dm.Connection() }.ExecuteDirect("DROP TABLE IF EXISTS \"BenchChild\"");
    std::ignore = SqlStatement { dm.Connection() }.ExecuteDirect("DROP TABLE IF EXISTS \"BenchOwner\"");
    dm.CreateTable<Owner>();
    dm.CreateTable<Child>();

    {
        auto transaction = SqlTransaction { dm.Connection() };
        for (size_t i = 0; i < owners; ++i)
        {
            auto owner = Owner { .name = SqlAnsiString<40> { std::format("owner-{}", i) } };
            dm.Create(owner);
            for (size_t j = 0; j < childrenPerOwner; ++j)
            {
                auto child = Child { .label = SqlAnsiString<40> { std::format("child-{}-{}", i, j) } };
                child.owner = owner;
                dm.Create(child);
            }
        }
        transaction.Commit();
    }
    // Optional: index the foreign key column. CreateTable() emits a FOREIGN KEY constraint but no
    // index, and none of the three supported engines indexes a foreign key implicitly - so this
    // isolates "N queries" from "N full table scans".
    if (char const* const withIndex = std::getenv("LW_BENCH_INDEX"); withIndex && *withIndex == '1')
        std::ignore =
            SqlStatement { dm.Connection() }.ExecuteDirect(R"(CREATE INDEX "ix_bench_owner" ON "BenchChild" ("owner_id"))");

    // Warm-up: opens the connection's first cursor and warms the server-side caches.
    (void) dm.Query<Owner, DataMapperOptions { .loadRelations = false }>().All();
    g_logger.Reset();

    std::printf("== %zu owners x %zu children (%s)\n", owners, childrenPerOwner, connectionString.c_str());

    // (1) Baseline: parents only, relations auto-loading configured (the default).
    {
        size_t rows = 0;
        auto const ms = TimeMs([&] { rows = dm.Query<Owner>().All().size(); });
        Report("parents only, loadRelations=true (default)", ms, rows);
    }

    // (2) Baseline: parents only, no loader installation at all.
    {
        size_t rows = 0;
        auto const ms = TimeMs([&] { rows = dm.Query<Owner, DataMapperOptions { .loadRelations = false }>().All().size(); });
        Report("parents only, loadRelations=false", ms, rows);
    }

    // (3) The N+1: touch HasMany on every parent.
    {
        size_t rows = 0;
        auto const ms = TimeMs([&] {
            auto parents = dm.Query<Owner>().All();
            for (auto& p: parents)
                rows += p.children.All().size();
        });
        Report("HasMany lazy per parent (N+1)", ms, rows);
    }

    // (4) The fix, done by hand: one extra SELECT ... WHERE owner_id IN (...) + in-memory stitch.
    {
        size_t rows = 0;
        auto const ms = TimeMs([&] {
            auto parents = dm.Query<Owner, DataMapperOptions { .loadRelations = false }>().All();
            std::vector<uint64_t> ids;
            ids.reserve(parents.size());
            for (auto const& p: parents)
                ids.push_back(p.id.Value());
            auto kids = dm.Query<Child, DataMapperOptions { .loadRelations = false }>()
                            .WhereIn(FieldNameOf<Member(Child::owner)>, ids)
                            .All();
            std::unordered_map<uint64_t, std::vector<std::shared_ptr<Child>>> byOwner;
            byOwner.reserve(parents.size());
            for (auto& k: kids)
                byOwner[k.owner.Value()].push_back(std::make_shared<Child>(std::move(k)));
            for (auto& p: parents)
            {
                auto it = byOwner.find(p.id.Value());
                if (it != byOwner.end())
                    rows += p.children.Emplace(std::move(it->second)).size();
            }
        });
        Report("HasMany batched WhereIn + stitch (2 queries)", ms, rows);
    }

    // (5) BelongsTo N+1: touch the parent of every child.
    {
        size_t rows = 0;
        auto const ms = TimeMs([&] {
            auto kids = dm.Query<Child>().All();
            for (auto& k: kids)
                rows += k.owner.Record().id.Value() != 0 ? 1 : 0;
        });
        Report("BelongsTo lazy per child (N+1)", ms, rows);
    }

    // (6) BelongsTo batched: distinct FK values, one IN query, distribute.
    {
        size_t rows = 0;
        auto const ms = TimeMs([&] {
            auto kids = dm.Query<Child, DataMapperOptions { .loadRelations = false }>().All();
            std::vector<uint64_t> ids;
            ids.reserve(kids.size());
            for (auto const& k: kids)
                ids.push_back(k.owner.Value());
            std::ranges::sort(ids);
            ids.erase(std::ranges::unique(ids).begin(), ids.end());
            auto parents = dm.Query<Owner, DataMapperOptions { .loadRelations = false }>()
                               .WhereIn(FieldNameOf<Member(Owner::id)>, ids)
                               .All();
            std::unordered_map<uint64_t, Owner*> byId;
            byId.reserve(parents.size());
            for (auto& p: parents)
                byId[p.id.Value()] = &p;
            for (auto& k: kids)
            {
                auto it = byId.find(k.owner.Value());
                if (it != byId.end())
                {
                    k.owner.AdoptFetchedRecord(*it->second);
                    ++rows;
                }
            }
        });
        Report("BelongsTo batched WhereIn + stitch (2 queries)", ms, rows);
    }

    // (7)/(8) How much of one lazy load is the re-Prepare? The lazy loaders call
    // SqlStatement::Prepare() on identical SQL for every owner; SqlStatement has no
    // prepared-statement cache, so each iteration re-issues SQLPrepareW.
    {
        auto const sql = std::format(R"(SELECT "id", "label", "owner_id" FROM "BenchChild" WHERE "owner_id" = ?)");
        size_t rows = 0;
        auto stmt = SqlStatement { dm.Connection() };
        auto const ms = TimeMs([&] {
            for (size_t i = 1; i <= owners; ++i)
            {
                stmt.Prepare(sql);
                auto cursor = stmt.Execute(static_cast<uint64_t>(i));
                while (cursor.FetchRow())
                    ++rows;
            }
        });
        Report("raw: Prepare+Execute per owner (N queries)", ms, rows);
    }
    {
        auto const sql = std::format(R"(SELECT "id", "label", "owner_id" FROM "BenchChild" WHERE "owner_id" = ?)");
        size_t rows = 0;
        auto stmt = SqlStatement { dm.Connection() };
        auto const ms = TimeMs([&] {
            stmt.Prepare(sql);
            for (size_t i = 1; i <= owners; ++i)
            {
                auto cursor = stmt.Execute(static_cast<uint64_t>(i));
                while (cursor.FetchRow())
                    ++rows;
            }
        });
        Report("raw: Prepare once + Execute per owner", ms, rows);
    }

    // (9) The new API: HasMany eager-loaded for the whole batch.
    {
        size_t rows = 0;
        auto const ms = TimeMs([&] {
            auto parents = dm.Query<Owner>().With<Member(Owner::children)>().All();
            for (auto& p: parents)
                rows += p.children.All().size();
        });
        Report("HasMany .With<>() eager", ms, rows);
    }

    // (10) The new API: BelongsTo eager-loaded for the whole batch.
    {
        size_t rows = 0;
        auto const ms = TimeMs([&] {
            auto kids = dm.Query<Child>().With<Member(Child::owner)>().All();
            for (auto& k: kids)
                rows += k.owner.Record().id.Value() != 0 ? 1 : 0;
        });
        Report("BelongsTo .With<>() eager", ms, rows);
    }

    return 0;
}
