// SPDX-License-Identifier: Apache-2.0
//
// Eager relation loading: `Query<Record>().With<&Record::relation>()`.
//
// The point of the feature is the *number of queries*, so correctness alone is not enough to test:
// a `With<>()` that quietly fell back to the on-demand loaders would still return the right records
// while reintroducing exactly the N+1 the feature exists to remove. Every test here therefore
// asserts the statement count through a counting `SqlLogger` alongside the data.

#include "../Utils.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlLogger.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace Lightweight;

struct EagerChild;

struct EagerCategory
{
    static constexpr std::string_view TableName = "EagerCategory";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<32>> title {};
};

struct EagerOwner
{
    static constexpr std::string_view TableName = "EagerOwner";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<32>> name {};
    HasMany<EagerChild> children {};
};

struct EagerChild
{
    static constexpr std::string_view TableName = "EagerChild";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<32>> label {};
    BelongsTo<Member(EagerOwner::id), SqlRealName { "owner_id" }> owner {};
    BelongsTo<Member(EagerCategory::id), SqlRealName { "category_id" }, SqlNullable::Null> category {};
};

namespace
{

/// Counts the statements the mapper issues, so a test can assert "two queries" rather than trusting
/// that the batched path was taken.
///
/// Only `OnPrepare` and `OnExecuteDirect` are counted: every query the mapper runs passes through one
/// of the two, while `OnExecute` would double-count a prepared statement.
class StatementCountingLogger final: public SqlLogger::Null
{
  public:
    void OnPrepare(std::string_view const& /*query*/) override
    {
        ++_statements;
    }

    void OnExecuteDirect(std::string_view const& /*query*/) override
    {
        ++_statements;
    }

    /// Statements issued since the last Reset().
    [[nodiscard]] size_t Count() const noexcept
    {
        return _statements;
    }

    void Reset() noexcept
    {
        _statements = 0;
    }

  private:
    size_t _statements = 0;
};

/// Installs a statement-counting logger for the duration of a test, restoring the previous one.
class ScopedStatementCounter
{
  public:
    ScopedStatementCounter():
        _previous { &SqlLogger::GetLogger() }
    {
        SqlLogger::SetLogger(_counter);
    }

    ScopedStatementCounter(ScopedStatementCounter const&) = delete;
    ScopedStatementCounter(ScopedStatementCounter&&) = delete;
    ScopedStatementCounter& operator=(ScopedStatementCounter const&) = delete;
    ScopedStatementCounter& operator=(ScopedStatementCounter&&) = delete;

    ~ScopedStatementCounter()
    {
        SqlLogger::SetLogger(*_previous);
    }

    /// Statements issued since construction or the last Reset().
    [[nodiscard]] size_t Count() const noexcept
    {
        return _counter.Count();
    }

    void Reset() noexcept
    {
        _counter.Reset();
    }

  private:
    StatementCountingLogger _counter;
    SqlLogger* _previous;
};

/// Creates the two tables and fills them with @p ownerCount owners, each owning @p childrenPerOwner
/// children.
///
/// @param dm The mapper to create the schema and rows through.
/// @param ownerCount How many owner rows to create.
/// @param childrenPerOwner How many children each owner gets.
void MakeOwnersWithChildren(DataMapper& dm, size_t ownerCount, size_t childrenPerOwner)
{
    dm.CreateTable<EagerCategory>();
    dm.CreateTable<EagerOwner>();
    dm.CreateTable<EagerChild>();

    auto category = EagerCategory { .title = "shared" };
    dm.Create(category);

    for (size_t ownerIndex = 0; ownerIndex < ownerCount; ++ownerIndex)
    {
        auto owner = EagerOwner { .name = SqlAnsiString<32> { std::format("owner-{}", ownerIndex) } };
        dm.Create(owner);

        for (size_t childIndex = 0; childIndex < childrenPerOwner; ++childIndex)
        {
            auto child = EagerChild { .label = SqlAnsiString<32> { std::format("child-{}-{}", ownerIndex, childIndex) } };
            child.owner = owner;
            // Every other child leaves the optional relation NULL, so the batched loader has to cope
            // with a foreign key that references nothing.
            if (childIndex % 2 == 0)
                child.category = category;
            dm.Create(child);
        }
    }
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "With<HasMany> loads every owner's children in one extra query", "[DataMapper][With]")
{
    auto dm = DataMapper {};
    MakeOwnersWithChildren(dm, 5, 3);

    auto counter = ScopedStatementCounter {};
    auto owners = dm.Query<EagerOwner>().With<Member(EagerOwner::children)>().All();

    // One SELECT for the owners, one for all their children - regardless of how many owners there are.
    CHECK(counter.Count() == 2);

    REQUIRE(owners.size() == 5);
    for (auto& owner: owners)
    {
        CHECK(owner.children.Count() == 3);
        for (auto const& child: owner.children.All())
            CHECK(child->owner.Value() == owner.id.Value());
    }

    // Touching the relation must not have gone back to the database at all.
    CHECK(counter.Count() == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "With<BelongsTo> loads every child's owner in one extra query", "[DataMapper][With]")
{
    auto dm = DataMapper {};
    MakeOwnersWithChildren(dm, 4, 3);

    auto counter = ScopedStatementCounter {};
    auto children = dm.Query<EagerChild>().With<Member(EagerChild::owner)>().All();

    CHECK(counter.Count() == 2);

    REQUIRE(children.size() == 12);
    for (auto& child: children)
    {
        CHECK(child.owner.Record().id.Value() == child.owner.Value());
        CHECK(child.owner.Record().name.Value().ToStringView().starts_with("owner-"));
    }

    CHECK(counter.Count() == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "With<> chains across several relations", "[DataMapper][With]")
{
    auto dm = DataMapper {};
    MakeOwnersWithChildren(dm, 3, 2);

    auto counter = ScopedStatementCounter {};
    // Two relations named means two extra statements - not one per record, and not one per relation
    // per record.
    auto children =
        dm.Query<EagerChild>().With<Member(EagerChild::owner)>().With<Member(EagerChild::category)>().All();

    CHECK(counter.Count() == 3);
    REQUIRE(children.size() == 6);
    for (auto& child: children)
        CHECK(child.owner.Record().id.Value() == child.owner.Value());
}

TEST_CASE_METHOD(SqlTestFixture, "With<BelongsTo> skips rows whose foreign key is NULL", "[DataMapper][With]")
{
    auto dm = DataMapper {};
    MakeOwnersWithChildren(dm, 3, 2);

    auto counter = ScopedStatementCounter {};
    auto children = dm.Query<EagerChild>().With<Member(EagerChild::category)>().All();

    CHECK(counter.Count() == 2);
    REQUIRE(children.size() == 6);

    size_t withCategory = 0;
    for (auto& child: children)
    {
        if (!child.category.Value().has_value())
            continue;
        ++withCategory;

        // A nullable BelongsTo hands back an optional reference; throwing rather than CHECK-ing makes
        // the dereference below provably safe (a Catch2 assertion is a loop the analyser cannot follow).
        auto const category = child.category.Record();
        if (!category.has_value())
            throw std::runtime_error("The eagerly loaded category must be present.");
        CHECK(category->get().title.Value().ToStringView() == "shared");
    }
    // Half the children carry the optional relation; the loader must have fetched the one shared
    // category exactly once for all of them.
    CHECK(withCategory == 3);
    CHECK(counter.Count() == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "With<HasMany> marks childless owners loaded-empty", "[DataMapper][With]")
{
    auto dm = DataMapper {};
    MakeOwnersWithChildren(dm, 2, 2);

    // An owner with no children at all.
    auto lonely = EagerOwner { .name = "lonely" };
    dm.Create(lonely);

    auto counter = ScopedStatementCounter {};
    auto owners = dm.Query<EagerOwner>().With<Member(EagerOwner::children)>().All();
    REQUIRE(owners.size() == 3);

    auto const it =
        std::ranges::find_if(owners, [](EagerOwner const& owner) { return owner.name.Value().ToStringView() == "lonely"; });
    REQUIRE(it != owners.end());

    // The empty result is already known, so asking for it must not produce a query.
    CHECK(it->children.IsEmpty());
    CHECK(it->children.Count() == 0);
    CHECK(counter.Count() == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "With<> applies to First(n) and Range() too", "[DataMapper][With]")
{
    auto dm = DataMapper {};
    MakeOwnersWithChildren(dm, 6, 2);

    {
        auto counter = ScopedStatementCounter {};
        auto owners = dm.Query<EagerOwner>()
                          .OrderBy(FieldNameOf<Member(EagerOwner::id)>, SqlResultOrdering::ASCENDING)
                          .With<Member(EagerOwner::children)>()
                          .First(3);
        CHECK(counter.Count() == 2);
        REQUIRE(owners.size() == 3);
        for (auto& owner: owners)
            CHECK(owner.children.Count() == 2);
    }

    {
        auto counter = ScopedStatementCounter {};
        auto owners = dm.Query<EagerOwner>()
                          .OrderBy(FieldNameOf<Member(EagerOwner::id)>, SqlResultOrdering::ASCENDING)
                          .With<Member(EagerOwner::children)>()
                          .Range(2, 2);
        CHECK(counter.Count() == 2);
        REQUIRE(owners.size() == 2);
        for (auto& owner: owners)
            CHECK(owner.children.Count() == 2);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "With<> on an empty result set issues no relation query", "[DataMapper][With]")
{
    auto dm = DataMapper {};
    MakeOwnersWithChildren(dm, 0, 0);

    auto counter = ScopedStatementCounter {};
    auto owners = dm.Query<EagerOwner>().With<Member(EagerOwner::children)>().All();

    CHECK(owners.empty());
    CHECK(counter.Count() == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "With<> survives a batch larger than one IN chunk", "[DataMapper][With]")
{
    // The IN predicate is chunked (SqlQueryFormatter::MaxInPredicateValues), and the chunk boundary is
    // where a batched loader most easily loses rows: the last partial chunk, or an owner whose key
    // lands in a later chunk than the first.
    auto dm = DataMapper {};
    auto const ownerCount = size_t { 1005 };
    MakeOwnersWithChildren(dm, ownerCount, 1);

    auto counter = ScopedStatementCounter {};
    auto owners = dm.Query<EagerOwner>().With<Member(EagerOwner::children)>().All();

    REQUIRE(owners.size() == ownerCount);

    // 1 for the owners + ceil(1005 / 1000) for the children: a constant per chunk, never per record.
    CHECK(counter.Count() == 3);

    size_t childrenSeen = 0;
    for (auto& owner: owners)
        childrenSeen += owner.children.Count();
    CHECK(childrenSeen == ownerCount);
    CHECK(counter.Count() == 3);
}

TEST_CASE_METHOD(SqlTestFixture, "Relations not named by With<> keep loading on demand", "[DataMapper][With]")
{
    auto dm = DataMapper {};
    MakeOwnersWithChildren(dm, 3, 2);

    auto counter = ScopedStatementCounter {};
    auto children = dm.Query<EagerChild>().All();
    CHECK(counter.Count() == 1);

    // No With<> was requested, so the first access still goes to the database - the feature must not
    // change what an unrequested relation does.
    CHECK(children.front().owner.Record().id.Value() != 0);
    CHECK(counter.Count() > 1);
}
