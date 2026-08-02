// SPDX-License-Identifier: Apache-2.0
//
// Structural relation shapes: schema geometries the mapper has to get right, independently of which
// relation API is used to traverse them.
//
// The shapes are drawn from the scenarios a mature ORM test suite treats as its highest-risk cases
// (SQLAlchemy's `test/orm/test_cycles.py`, `test_relationships.py` and `test_eager_relations.py` are
// the reference), restricted to the ones Lightweight's relation model can express. `HasMany` is a
// read-only view, so the cascade / orphan-detection / backref-fixup families do not apply; composite
// foreign keys and foreign keys onto a non-primary-key column are not representable at all.
//
// `RelationTests.cpp` already covers how a relation finds its inverse - by type, with a selector to
// disambiguate several foreign keys into one table. What this file covers instead is the *shape of
// the schema graph*: self-reference, depth, and nullability.
//
// Two defects are documented here as tests rather than as prose, so that a fix has an executable
// definition of done and a regression re-breaks the build:
//
//   1. A record whose `BelongsTo` names its own enclosing type cannot be used at all - see
//      "Self-reference" below. This is why the self-referential shape is declared but its tests are
//      compiled out: the failure is a stack overflow during static initialisation, which takes the
//      whole test binary down rather than failing one assertion, so `[!mayfail]` cannot contain it.
//   2. `LoadRelations()` does not compile for any record holding a `BelongsTo` - see
//      "LoadRelations".

#include "../Utils.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using namespace Lightweight;
using namespace std::string_view_literals;

// ================================================================================================
// Self-reference: one table whose foreign key points at its own primary key.
//
// The commonest self-referential shape in a real schema - a tree: category parent, employee manager,
// comment thread - and one ddl2cpp already generates code for. `CxxModelPrinter` has explicit
// `selfReferencing()` handling and `CxxModelPrinterTests.cpp` asserts that such a column becomes a
// `BelongsTo` contributing no `#include` of its own. So the generator emits this record; nothing
// ever fed one to a DataMapper.
//
// DEFECT: it does not work. Declaring
//
//     struct TreeNode
//     {
//         Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
//         Field<SqlAnsiString<30>> name {};
//         BelongsTo<Member(TreeNode::id), SqlRealName { "parent_id" }, SqlNullable::Null> parent {};
//         HasMany<TreeNode> children {};
//     };
//
// and merely *constructing* one overflows the stack. Reduced to its minimum, the trigger needs
// neither `HasMany`, nor a DataMapper, nor a database:
//
//   - `sizeof(TreeNode)` alone is fine and reports 64, so the class layout is not the recursion.
//   - Default-constructing a `TreeNode` in a test body crashes before the body's first statement
//     runs, i.e. during static initialisation of the translation unit.
//   - The identical record with its `BelongsTo` pointing at a *different* type - the pattern every
//     pre-existing test uses - constructs and assigns fine. The self-reference is the whole
//     difference.
//
// `BelongsTo` owns a `std::unique_ptr<ReferencedRecord>`, so a self-referential instantiation makes
// the record reachable from itself; some member of the surrounding machinery instantiated over that
// cycle has no base case. Where exactly is for the fix to establish.
//
// Kept as a compiled-out block rather than a `[!mayfail]` test: a stack overflow in static
// initialisation aborts the whole binary, so an enabled version would take all ~1300 unrelated test
// cases with it. Re-enable this block together with the fix; the assertions are written to pass once
// the shape works.
// ================================================================================================

#if 0
struct TreeNode
{
    static constexpr std::string_view TableName = "TreeNodes";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};
    BelongsTo<Member(TreeNode::id), SqlRealName { "parent_id" }, SqlNullable::Null> parent {};
    HasMany<TreeNode> children {};
};

// The inverse of a self-referential HasMany is the record's own BelongsTo. Resolution is by type, so
// pointing at one's own table must not be a special case for the resolver: it has to land on
// parent_id rather than on some other member.
static_assert(InverseBelongsToIndexOf<TreeNode, TreeNode> == 2);
static_assert(InverseBelongsToFieldNameOf<TreeNode, TreeNode> == "parent_id"sv);

namespace
{

struct TreeIds
{
    uint64_t root {};
    uint64_t childA {};
    uint64_t childB {};
    uint64_t grandchild {};
};

/// Builds a two-level tree: a root with two children, and one grandchild under the first child.
///
/// @param dm Mapper to create the records through.
/// @return The ids of root, first child, second child and grandchild.
TreeIds MakeTree(DataMapper& dm)
{
    auto root = TreeNode { .name = "root" };
    dm.Create(root);

    auto childA = TreeNode { .name = "childA" };
    childA.parent = root;
    dm.Create(childA);

    auto childB = TreeNode { .name = "childB" };
    childB.parent = root;
    dm.Create(childB);

    auto grandchild = TreeNode { .name = "grandchild" };
    grandchild.parent = childA;
    dm.Create(grandchild);

    return TreeIds { .root = root.id.Value(),
                     .childA = childA.id.Value(),
                     .childB = childB.id.Value(),
                     .grandchild = grandchild.id.Value() };
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "Self-referencing record round-trips its foreign key", "[DataMapper][relations][selfref]")
{
    // The plain-column half of the shape, involving no relation loader at all: a row whose foreign
    // key names another row of the same table must store and re-read that key, and the root's NULL
    // parent must come back empty rather than as 0.
    auto dm = DataMapper {};
    dm.CreateTable<TreeNode>();

    auto const ids = MakeTree(dm);

    auto const root = dm.QuerySingle<TreeNode>(ids.root);
    REQUIRE(root.has_value());
    CHECK(root->name.Value() == "root");
    CHECK_FALSE(root->parent.Value().has_value());

    auto const childA = dm.QuerySingle<TreeNode>(ids.childA);
    REQUIRE(childA.has_value());
    REQUIRE(childA->parent.Value().has_value());
    CHECK(childA->parent.Value().value() == ids.root);

    auto const grandchild = dm.QuerySingle<TreeNode>(ids.grandchild);
    REQUIRE(grandchild.has_value());
    REQUIRE(grandchild->parent.Value().has_value());
    CHECK(grandchild->parent.Value().value() == ids.childA); // two levels down, not collapsed to the root
}

TEST_CASE_METHOD(SqlTestFixture, "Self-referencing HasMany counts only direct children", "[DataMapper][relations][selfref]")
{
    // A tree is the shape that exposes an inverse resolved to the wrong column: if the WHERE clause
    // named the primary key instead of parent_id, the root would claim every row in the table.
    auto dm = DataMapper {};
    dm.CreateTable<TreeNode>();

    auto const ids = MakeTree(dm);

    auto root = dm.QuerySingle<TreeNode>(ids.root);
    REQUIRE(root.has_value());
    CHECK(root->children.Count() == 2); // childA and childB - not the grandchild, not all four rows

    auto childA = dm.QuerySingle<TreeNode>(ids.childA);
    REQUIRE(childA.has_value());
    CHECK(childA->children.Count() == 1);

    auto childB = dm.QuerySingle<TreeNode>(ids.childB);
    REQUIRE(childB.has_value());
    CHECK(childB->children.Count() == 0); // a leaf

    auto grandchild = dm.QuerySingle<TreeNode>(ids.grandchild);
    REQUIRE(grandchild.has_value());
    CHECK(grandchild->children.Count() == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "Self-referencing HasMany yields children through All()", "[DataMapper][relations][selfref]")
{
    auto dm = DataMapper {};
    dm.CreateTable<TreeNode>();

    auto const ids = MakeTree(dm);

    auto root = dm.QuerySingle<TreeNode>(ids.root);
    REQUIRE(root.has_value());

    auto names = std::set<std::string> {};
    for (auto const& child: root->children.All())
        names.emplace(child->name.Value());

    CHECK(names == std::set<std::string> { "childA", "childB" });
}

TEST_CASE_METHOD(SqlTestFixture, "Self-referencing HasMany is traversable with Each()", "[DataMapper][relations][selfref]")
{
    // Each() is the branch most at risk even once construction works: the auto-loader's `.each`
    // lambda calls ConfigureRelationAutoLoading() on every child it materialises, and for a
    // self-referential record that child is the same type as its parent - so it is handed a loader
    // for its own children, which configures its children, without a base case. Count() and All()
    // do not configure what they return, so they are expected to survive where this does not.
    auto dm = DataMapper {};
    dm.CreateTable<TreeNode>();

    auto const ids = MakeTree(dm);

    auto root = dm.QuerySingle<TreeNode>(ids.root);
    REQUIRE(root.has_value());

    auto names = std::set<std::string> {};
    root->children.Each([&](TreeNode const& child) { names.emplace(child.name.Value()); });

    CHECK(names == std::set<std::string> { "childA", "childB" });
}

TEST_CASE_METHOD(SqlTestFixture, "A NULL foreign key is attributed to no parent", "[DataMapper][relations][selfref]")
{
    // A detached row must not be swept into some parent's collection. The failure mode guarded
    // against is a predicate that compares against NULL in a way the backend treats as a match, or a
    // loader that drops the predicate when the key is absent - either way the orphan would surface as
    // a child of whichever row was queried.
    auto dm = DataMapper {};
    dm.CreateTable<TreeNode>();

    auto root = TreeNode { .name = "root" };
    dm.Create(root);

    auto orphan = TreeNode { .name = "orphan" }; // parent deliberately left unset
    dm.Create(orphan);

    auto loadedRoot = dm.QuerySingle<TreeNode>(root.id.Value());
    REQUIRE(loadedRoot.has_value());
    CHECK(loadedRoot->children.Count() == 0);
    CHECK(loadedRoot->children.All().empty());

    auto loadedOrphan = dm.QuerySingle<TreeNode>(orphan.id.Value());
    REQUIRE(loadedOrphan.has_value());
    CHECK_FALSE(loadedOrphan->parent.Value().has_value());
    CHECK(loadedOrphan->children.Count() == 0);
}
#endif // self-referential BelongsTo is unusable - see the comment block above

TEST_CASE("Self-referential BelongsTo is unusable", "[DataMapper][relations][selfref][!shouldfail]")
{
    // Placeholder that keeps the defect visible in the test report while the block above cannot be
    // compiled. `[!shouldfail]` inverts the result, so this passes the run *because* it fails - and
    // starts failing the moment someone deletes it without re-enabling the block.
    FAIL("A record whose BelongsTo names its own type overflows the stack on construction; the "
         "self-referential shape tests in RelationShapeTests.cpp are compiled out. See the comment "
         "block above this test.");
}

// ================================================================================================
// Nullability, without a self-reference: two distinct tables, nullable foreign key.
//
// This isolates the nullable-foreign-key semantics from the self-reference defect above, so the NULL
// handling is covered by something that actually runs today.
// ================================================================================================

struct OptionalParent
{
    static constexpr std::string_view TableName = "OptionalParents";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};
    HasMany<struct OptionalChild> children {};
};

struct OptionalChild
{
    static constexpr std::string_view TableName = "OptionalChildren";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<30>> name {};
    BelongsTo<Member(OptionalParent::id), SqlRealName { "parent_id" }, SqlNullable::Null> parent {};
};

static_assert(InverseBelongsToFieldNameOf<OptionalParent, OptionalChild> == "parent_id"sv);

TEST_CASE_METHOD(SqlTestFixture, "A NULL foreign key belongs to no parent", "[DataMapper][relations][nullable]")
{
    auto dm = DataMapper {};
    dm.CreateTable<OptionalParent>();
    dm.CreateTable<OptionalChild>();

    auto parent = OptionalParent { .name = "parent" };
    dm.Create(parent);

    auto attached = OptionalChild { .name = "attached" };
    attached.parent = parent;
    dm.Create(attached);

    auto orphan = OptionalChild { .name = "orphan" }; // parent deliberately left unset
    dm.Create(orphan);

    // The orphan must not be counted as a child of the only existing parent.
    auto loadedParent = dm.QuerySingle<OptionalParent>(parent.id.Value());
    REQUIRE(loadedParent.has_value());
    CHECK(loadedParent->children.Count() == 1);

    auto names = std::set<std::string> {};
    for (auto const& child: loadedParent->children.All())
        names.emplace(child->name.Value());
    CHECK(names == std::set<std::string> { "attached" });

    // ...and the orphan reads back as genuinely unset, not as parent id 0.
    auto loadedOrphan = dm.QuerySingle<OptionalChild>(orphan.id.Value());
    REQUIRE(loadedOrphan.has_value());
    CHECK_FALSE(loadedOrphan->parent.Value().has_value());

    auto loadedAttached = dm.QuerySingle<OptionalChild>(attached.id.Value());
    REQUIRE(loadedAttached.has_value());
    REQUIRE(loadedAttached->parent.Value().has_value());
    CHECK(loadedAttached->parent.Value().value() == parent.id.Value());
}

TEST_CASE_METHOD(SqlTestFixture, "A nullable foreign key can be cleared", "[DataMapper][relations][nullable]")
{
    // Detaching a child by assigning SqlNull must remove it from the parent's collection rather than
    // leave a dangling key behind.
    auto dm = DataMapper {};
    dm.CreateTable<OptionalParent>();
    dm.CreateTable<OptionalChild>();

    auto parent = OptionalParent { .name = "parent" };
    dm.Create(parent);

    auto child = OptionalChild { .name = "child" };
    child.parent = parent;
    dm.Create(child);

    auto beforeDetach = dm.QuerySingle<OptionalParent>(parent.id.Value());
    REQUIRE(beforeDetach.has_value());
    REQUIRE(beforeDetach->children.Count() == 1);

    child.parent = SqlNullValue;
    dm.Update(child);

    auto afterDetach = dm.QuerySingle<OptionalParent>(parent.id.Value());
    REQUIRE(afterDetach.has_value());
    CHECK(afterDetach->children.Count() == 0);

    auto detachedChild = dm.QuerySingle<OptionalChild>(child.id.Value());
    REQUIRE(detachedChild.has_value());
    CHECK_FALSE(detachedChild->parent.Value().has_value());
}

// ================================================================================================
// Depth: a chain of distinct tables, A -> B -> C -> D.
//
// Path resolution at depth >= 3 is where an ORM tends to pick the wrong entity, because each hop has
// to resolve against its own pair of records rather than against the one the traversal started from.
// ================================================================================================

struct ChainB;
struct ChainC;
struct ChainD;

struct ChainA
{
    static constexpr std::string_view TableName = "ChainAs";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<20>> label {};
    HasMany<ChainB> bs {};
};

struct ChainB
{
    static constexpr std::string_view TableName = "ChainBs";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<20>> label {};
    BelongsTo<Member(ChainA::id)> a {};
    HasMany<ChainC> cs {};
};

struct ChainC
{
    static constexpr std::string_view TableName = "ChainCs";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<20>> label {};
    BelongsTo<Member(ChainB::id)> b {};
    HasMany<ChainD> ds {};
};

struct ChainD
{
    static constexpr std::string_view TableName = "ChainDs";

    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id {};
    Field<SqlAnsiString<20>> label {};
    BelongsTo<Member(ChainC::id)> c {};
};

// Each hop resolves against its own record pair, and each must name the column of the correct table.
static_assert(InverseBelongsToFieldNameOf<ChainA, ChainB> == "a"sv);
static_assert(InverseBelongsToFieldNameOf<ChainB, ChainC> == "b"sv);
static_assert(InverseBelongsToFieldNameOf<ChainC, ChainD> == "c"sv);

TEST_CASE_METHOD(SqlTestFixture, "Relations resolve along a four-table chain", "[DataMapper][relations][chain]")
{
    auto dm = DataMapper {};
    dm.CreateTable<ChainA>();
    dm.CreateTable<ChainB>();
    dm.CreateTable<ChainC>();
    dm.CreateTable<ChainD>();

    auto a = ChainA { .label = "a" };
    dm.Create(a);

    auto b = ChainB { .label = "b" };
    b.a = a;
    dm.Create(b);

    auto c = ChainC { .label = "c" };
    c.b = b;
    dm.Create(c);

    auto d = ChainD { .label = "d" };
    d.c = c;
    dm.Create(d);

    // A second branch at the B level: a hop resolving to the wrong table shows up as a count of 2
    // where 1 is correct.
    auto b2 = ChainB { .label = "b2" };
    b2.a = a;
    dm.Create(b2);

    auto loadedA = dm.QuerySingle<ChainA>(a.id.Value());
    REQUIRE(loadedA.has_value());
    CHECK(loadedA->bs.Count() == 2);

    auto loadedB = dm.QuerySingle<ChainB>(b.id.Value());
    REQUIRE(loadedB.has_value());
    CHECK(loadedB->cs.Count() == 1);
    CHECK(loadedB->a.Value() == a.id.Value());

    auto loadedC = dm.QuerySingle<ChainC>(c.id.Value());
    REQUIRE(loadedC.has_value());
    CHECK(loadedC->ds.Count() == 1);
    CHECK(loadedC->b.Value() == b.id.Value());

    auto loadedD = dm.QuerySingle<ChainD>(d.id.Value());
    REQUIRE(loadedD.has_value());
    CHECK(loadedD->c.Value() == c.id.Value());

    // The sibling branch has no C beneath it, which a hop ignoring its own foreign key would get
    // wrong by reporting the other branch's children.
    auto loadedB2 = dm.QuerySingle<ChainB>(b2.id.Value());
    REQUIRE(loadedB2.has_value());
    CHECK(loadedB2->cs.Count() == 0);
}

TEST_CASE_METHOD(SqlTestFixture, "Each level of a chain traverses independently", "[DataMapper][relations][chain]")
{
    // Two full branches, so that every level has a sibling. A hop that resolved against the wrong
    // record pair would cross the branches and double the counts.
    auto dm = DataMapper {};
    dm.CreateTable<ChainA>();
    dm.CreateTable<ChainB>();
    dm.CreateTable<ChainC>();
    dm.CreateTable<ChainD>();

    auto a = ChainA { .label = "a" };
    dm.Create(a);

    auto makeBranch = [&](std::string_view label) {
        auto b = ChainB { .label = SqlAnsiString<20> { label } };
        b.a = a;
        dm.Create(b);
        auto c = ChainC { .label = SqlAnsiString<20> { label } };
        c.b = b;
        dm.Create(c);
        auto d = ChainD { .label = SqlAnsiString<20> { label } };
        d.c = c;
        dm.Create(d);
        return std::tuple { b.id.Value(), c.id.Value(), d.id.Value() };
    };

    auto const [b1, c1, d1] = makeBranch("left");
    auto const [b2, c2, d2] = makeBranch("right");

    auto loadedA = dm.QuerySingle<ChainA>(a.id.Value());
    REQUIRE(loadedA.has_value());
    CHECK(loadedA->bs.Count() == 2);

    // Each B sees exactly its own C, not both.
    for (auto const bId: { b1, b2 })
    {
        auto loadedB = dm.QuerySingle<ChainB>(bId);
        REQUIRE(loadedB.has_value());
        CHECK(loadedB->cs.Count() == 1);
    }

    // ...and each C exactly its own D.
    for (auto const cId: { c1, c2 })
    {
        auto loadedC = dm.QuerySingle<ChainC>(cId);
        REQUIRE(loadedC.has_value());
        CHECK(loadedC->ds.Count() == 1);
    }

    auto loadedD1 = dm.QuerySingle<ChainD>(d1);
    REQUIRE(loadedD1.has_value());
    CHECK(loadedD1->c.Value() == c1);

    auto loadedD2 = dm.QuerySingle<ChainD>(d2);
    REQUIRE(loadedD2.has_value());
    CHECK(loadedD2->c.Value() == c2);
}

// ================================================================================================
// LoadRelations
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "LoadRelations on a record without a BelongsTo", "[DataMapper][relations][LoadRelations]")
{
    // ChainA holds only a Field and a HasMany, which is the shape LoadRelations() does instantiate
    // for. This is the boundary of the defect documented below: passing ChainB instead, which is the
    // same shape plus one non-nullable BelongsTo, does not compile.
    auto dm = DataMapper {};
    dm.CreateTable<ChainA>();
    dm.CreateTable<ChainB>();

    auto a = ChainA { .label = "a" };
    dm.Create(a);
    auto b = ChainB { .label = "b" };
    b.a = a;
    dm.Create(b);

    auto loadedA = dm.QuerySingle<ChainA>(a.id.Value()).value();
    dm.LoadRelations(loadedA);
    REQUIRE(loadedA.bs.All().size() == 1);
    CHECK(loadedA.bs.At(0).label.Value() == "b");
}

TEST_CASE("LoadRelations does not compile for a record holding a BelongsTo",
          "[DataMapper][relations][LoadRelations][!shouldfail]")
{
    // DEFECT, independent of the self-reference above and much wider in scope - it is *every* record
    // holding a BelongsTo, nullable or not:
    //
    //   DataMapper.hpp(2758): error: no viable overloaded '='
    //     field = LoadBelongsTo<FieldType>(field.Value());
    //
    // LoadBelongsTo() returns std::optional<ReferencedRecord>, while BelongsTo declares operator=
    // only for SqlNullType, for ReferencedRecord&, and for another BelongsTo - an optional converts
    // to none of them. So LoadRelations() instantiates only for records whose members are all plain
    // Fields plus HasMany/HasManyThrough/HasOneThrough relations; add one BelongsTo and the call is
    // ill-formed. LoadHasMany() is private, so there is no other public way to fill such a record's
    // relations.
    //
    // Latent because all three pre-existing LoadRelations() call sites in RelationTests.cpp pass a
    // record with no BelongsTo at all: MisalignedDepartment, Human and Suppliers each hold only
    // Fields plus HasMany members. The nullable BelongsTo on MisalignedEmployee sits on the *child*,
    // which LoadRelations never enumerates.
    //
    // Verified against both nullability flavours - OptionalChild (nullable) and ChainB
    // (non-nullable) - so the fix must cover both. Enable these lines with it:
    //
    //     auto loadedB = dm.QuerySingle<ChainB>(b.id.Value()).value();
    //     dm.LoadRelations(loadedB);
    //     CHECK(loadedB.cs.All().size() == 1);
    //     CHECK(loadedB.a.Value() == a.id.Value());
    //
    //     auto loadedChild = dm.QuerySingle<OptionalChild>(child.id.Value()).value();
    //     dm.LoadRelations(loadedChild);
    //     CHECK(loadedChild.parent.Value().has_value());
    //
    // `[!shouldfail]` because the failure is a hard compile error: it cannot be expressed as a
    // running assertion, so the test stands in for it and keeps it in the report.
    FAIL("LoadRelations() does not compile for any record holding a BelongsTo - see the comment above");
}
