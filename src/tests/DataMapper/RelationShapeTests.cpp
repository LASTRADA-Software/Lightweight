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
// The self-referential shape in the first section is guarded off under clang-cl, which miscompiles
// it - the diagnosis is in the comment block above that guard. It compiles and passes under MSVC
// `cl`, so the guard is keyed to the toolchain rather than the shape being unsupported.

#include "../Utils.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <stdexcept>
#include <string>

using namespace Lightweight;

/// Unwraps an optional that must hold a value, failing the test if it does not.
///
/// `REQUIRE(x.has_value()); x->member` reads naturally but spreads the check and the access across two
/// statements, which `bugprone-unchecked-optional-access` cannot follow - it flags every subsequent
/// `x->`. Routing every unwrap through this one helper keeps that pattern out of the tests entirely.
///
/// The throw, rather than relying on the `REQUIRE` above it, is what makes the access provably safe:
/// Catch2's `REQUIRE` expands to a loop the analyser cannot prove exits, so it does not count as a
/// guard. Throwing first means the dereference is unconditionally reachable only when a value is
/// present. Catch2 reports an escaped exception as a test failure, so the diagnostic is equivalent -
/// and this is a real guarantee rather than a NOLINT hiding the question.
///
/// @param value The optional to unwrap.
/// @return A reference to the contained value.
template <typename T>
[[nodiscard]] T const& ValueOf(std::optional<T> const& value)
{
    if (!value.has_value())
        throw std::runtime_error("Expected the optional to hold a value.");
    return *value;
}

/// Mutable overload, for the relation accessors that are not const (e.g. `HasMany::Each`, which loads
/// on demand).
///
/// @param value The optional to unwrap.
/// @return A reference to the contained value.
template <typename T>
[[nodiscard]] T& ValueOf(std::optional<T>& value)
{
    if (!value.has_value())
        throw std::runtime_error("Expected the optional to hold a value.");
    return *value;
}

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
// The shape itself is sound: under MSVC `cl` the tests below pass, and `sizeof(TreeNode)` is a
// well-behaved 120 bytes. What breaks is **clang-cl 22.1.3 code generation**, so the block is
// guarded on that toolchain rather than removed.
//
// Symptom under clang-cl: a function that constructs one of these records gets a nonsense frame
// size, and faults in its own prologue.
//
//     warning: stack frame size (18097232330883560) exceeds limit (4294967295)
//              in 'CATCH2_INTERNAL_TEST_0' [-Wframe-larger-than]
//
// Established with cdb against a symbolised debug build:
//
//   - The fault is `Stack overflow (c00000fd)` in `__chkstk+0x37`, reached from
//     `CATCH2_INTERNAL_TEST_0+0x10` - i.e. the stack probe in the *prologue*, before the body runs.
//     That is why no printf ever appeared regardless of placement.
//   - The stack is eight frames deep, not thousands, so nothing recurses at run time.
//   - Disassembly of the prologue shows the frame size as a baked-in immediate with no relocation:
//         movabsq $0x404b56408001e0, %rax ; callq __chkstk ; subq %rax, %rsp
//   - Every other `movabsq` in the same function shares the high half (`0x404b5640_80…`) and differs
//     only in the low bytes (0x1e0, 0x158, 0x4a, 0x140, 0x148, 0x1e8) - and those low values are
//     plausible frame offsets. The high half also *changes between builds* (0x404b5640…,
//     0x3ef21fe0…, 0x3ef26540…). So a small correct offset is being OR-ed with garbage, which is a
//     miscompile, not a computed size.
//   - `sizeof` alone is fine; heap-allocating instead of stack-allocating does not help, because it
//     is the enclosing frame that is mis-sized, not the object.
//   - The identical record whose `BelongsTo` points at a *different* type is fine on every
//     toolchain, which is why no pre-existing test tripped this.
//
// Also ruled out as sufficient causes, each by isolated reproduction that compiles and runs
// correctly: `BelongsTo`'s variadic converting constructor; the
// ConfigureRelationAutoLoading -> LoadBelongsTo -> QuerySingle cycle;
// `std::function<std::optional<Record>()>` held by value inside the record it returns; and
// reflection-cpp's `CountMembers` probe against a constructor that absorbs `AnyType`.
//
// So there is nothing to fix in Lightweight here. The remaining work is to reduce this to a
// standalone case and report it upstream to LLVM, and meanwhile to keep the guard below so the
// coverage runs on the toolchains that handle it. A compiled-out block rather than `[!mayfail]`
// because a prologue fault aborts the whole binary, taking ~1300 unrelated cases with it.
// ================================================================================================

// clang-cl 22.1.3 miscompiles this shape: see the comment block above. MSVC cl and (per the isolated
// repros) libstdc++/clang++ handle it correctly, so the tests run everywhere except clang-cl.
#if defined(__clang__) && defined(_MSC_VER)
    #define LIGHTWEIGHT_SELFREF_BELONGSTO_MISCOMPILED 1
#endif

#if !defined(LIGHTWEIGHT_SELFREF_BELONGSTO_MISCOMPILED)
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

    auto rootOpt = dm.QuerySingle<TreeNode>(ids.root);
    auto& root = ValueOf(rootOpt);
    CHECK(root.name.Value() == "root");
    CHECK_FALSE(root.parent.Value().has_value());

    auto childAOpt = dm.QuerySingle<TreeNode>(ids.childA);
    auto& childA = ValueOf(childAOpt);
    CHECK(ValueOf(childA.parent.Value()) == ids.root);

    auto grandchildOpt = dm.QuerySingle<TreeNode>(ids.grandchild);
    auto& grandchild = ValueOf(grandchildOpt);
    CHECK(ValueOf(grandchild.parent.Value()) == ids.childA); // two levels down, not collapsed to the root
}

TEST_CASE_METHOD(SqlTestFixture, "Self-referencing HasMany counts only direct children", "[DataMapper][relations][selfref]")
{
    // A tree is the shape that exposes an inverse resolved to the wrong column: if the WHERE clause
    // named the primary key instead of parent_id, the root would claim every row in the table.
    auto dm = DataMapper {};
    dm.CreateTable<TreeNode>();

    auto const ids = MakeTree(dm);

    auto rootOpt = dm.QuerySingle<TreeNode>(ids.root);
    auto& root = ValueOf(rootOpt);
    CHECK(root.children.Count() == 2); // childA and childB - not the grandchild, not all four rows

    auto childAOpt = dm.QuerySingle<TreeNode>(ids.childA);
    auto& childA = ValueOf(childAOpt);
    CHECK(childA.children.Count() == 1);

    auto childBOpt = dm.QuerySingle<TreeNode>(ids.childB);
    auto& childB = ValueOf(childBOpt);
    CHECK(childB.children.Count() == 0); // a leaf

    auto grandchildOpt = dm.QuerySingle<TreeNode>(ids.grandchild);
    auto& grandchild = ValueOf(grandchildOpt);
    CHECK(grandchild.children.Count() == 0);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "Self-referencing HasMany yields children through All()",
                 "[DataMapper][relations][selfref]")
{
    auto dm = DataMapper {};
    dm.CreateTable<TreeNode>();

    auto const ids = MakeTree(dm);

    auto rootOpt = dm.QuerySingle<TreeNode>(ids.root);
    auto& root = ValueOf(rootOpt);

    auto names = std::set<std::string> {};
    for (auto const& child: root.children.All())
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

    auto rootOpt = dm.QuerySingle<TreeNode>(ids.root);
    auto& root = ValueOf(rootOpt);

    auto names = std::set<std::string> {};
    root.children.Each([&](TreeNode const& child) { names.emplace(child.name.Value()); });

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

    auto loadedRootOpt = dm.QuerySingle<TreeNode>(root.id.Value());
    auto& loadedRoot = ValueOf(loadedRootOpt);
    CHECK(loadedRoot.children.Count() == 0);
    CHECK(loadedRoot.children.All().empty());

    auto loadedOrphanOpt = dm.QuerySingle<TreeNode>(orphan.id.Value());
    auto& loadedOrphan = ValueOf(loadedOrphanOpt);
    CHECK_FALSE(loadedOrphan.parent.Value().has_value());
    CHECK(loadedOrphan.children.Count() == 0);
}
#endif // !LIGHTWEIGHT_SELFREF_BELONGSTO_MISCOMPILED

#if defined(LIGHTWEIGHT_SELFREF_BELONGSTO_MISCOMPILED)
TEST_CASE("Self-referential BelongsTo is miscompiled by clang-cl", "[DataMapper][relations][selfref][!shouldfail]")
{
    // Placeholder that keeps the skipped coverage visible in the report on the one toolchain where it
    // cannot be compiled. `[!shouldfail]` inverts the result, so this passes the run *because* it
    // fails - and starts failing the moment the guard is removed without the coverage coming back.
    //
    // It is deliberately absent under MSVC cl, where the guarded tests above do run.
    FAIL("clang-cl 22.1.3 computes a ~1.8e16-byte stack frame for any function constructing a record "
         "whose BelongsTo names its own type, and faults in the prologue. The self-referential shape "
         "tests are compiled out here but do run under MSVC cl. See the comment block above.");
}
#endif

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
    auto loadedParentOpt = dm.QuerySingle<OptionalParent>(parent.id.Value());
    auto& loadedParent = ValueOf(loadedParentOpt);
    CHECK(loadedParent.children.Count() == 1);

    auto names = std::set<std::string> {};
    for (auto const& child: loadedParent.children.All())
        names.emplace(child->name.Value());
    CHECK(names == std::set<std::string> { "attached" });

    // ...and the orphan reads back as genuinely unset, not as parent id 0.
    auto loadedOrphanOpt = dm.QuerySingle<OptionalChild>(orphan.id.Value());
    auto& loadedOrphan = ValueOf(loadedOrphanOpt);
    CHECK_FALSE(loadedOrphan.parent.Value().has_value());

    auto loadedAttachedOpt = dm.QuerySingle<OptionalChild>(attached.id.Value());
    auto& loadedAttached = ValueOf(loadedAttachedOpt);
    CHECK(ValueOf(loadedAttached.parent.Value()) == parent.id.Value());
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

    auto beforeDetachOpt = dm.QuerySingle<OptionalParent>(parent.id.Value());
    auto& beforeDetach = ValueOf(beforeDetachOpt);
    REQUIRE(beforeDetach.children.Count() == 1);

    child.parent = SqlNullValue;
    dm.Update(child);

    auto afterDetachOpt = dm.QuerySingle<OptionalParent>(parent.id.Value());
    auto& afterDetach = ValueOf(afterDetachOpt);
    CHECK(afterDetach.children.Count() == 0);

    auto detachedChildOpt = dm.QuerySingle<OptionalChild>(child.id.Value());
    auto& detachedChild = ValueOf(detachedChildOpt);
    CHECK_FALSE(detachedChild.parent.Value().has_value());
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

    auto loadedAOpt = dm.QuerySingle<ChainA>(a.id.Value());
    auto& loadedA = ValueOf(loadedAOpt);
    CHECK(loadedA.bs.Count() == 2);

    auto loadedBOpt = dm.QuerySingle<ChainB>(b.id.Value());
    auto& loadedB = ValueOf(loadedBOpt);
    CHECK(loadedB.cs.Count() == 1);
    CHECK(loadedB.a.Value() == a.id.Value());

    auto loadedCOpt = dm.QuerySingle<ChainC>(c.id.Value());
    auto& loadedC = ValueOf(loadedCOpt);
    CHECK(loadedC.ds.Count() == 1);
    CHECK(loadedC.b.Value() == b.id.Value());

    auto loadedDOpt = dm.QuerySingle<ChainD>(d.id.Value());
    auto& loadedD = ValueOf(loadedDOpt);
    CHECK(loadedD.c.Value() == c.id.Value());

    // The sibling branch has no C beneath it, which a hop ignoring its own foreign key would get
    // wrong by reporting the other branch's children.
    auto loadedB2Opt = dm.QuerySingle<ChainB>(b2.id.Value());
    auto& loadedB2 = ValueOf(loadedB2Opt);
    CHECK(loadedB2.cs.Count() == 0);
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

    auto loadedAOpt = dm.QuerySingle<ChainA>(a.id.Value());
    auto& loadedA = ValueOf(loadedAOpt);
    CHECK(loadedA.bs.Count() == 2);

    // Each B sees exactly its own C, not both.
    for (auto const bId: { b1, b2 })
    {
        auto loadedBOpt = dm.QuerySingle<ChainB>(bId);
        auto& loadedB = ValueOf(loadedBOpt);
        CHECK(loadedB.cs.Count() == 1);
    }

    // ...and each C exactly its own D.
    for (auto const cId: { c1, c2 })
    {
        auto loadedCOpt = dm.QuerySingle<ChainC>(cId);
        auto& loadedC = ValueOf(loadedCOpt);
        CHECK(loadedC.ds.Count() == 1);
    }

    auto loadedD1Opt = dm.QuerySingle<ChainD>(d1);
    auto& loadedD1 = ValueOf(loadedD1Opt);
    CHECK(loadedD1.c.Value() == c1);

    auto loadedD2Opt = dm.QuerySingle<ChainD>(d2);
    auto& loadedD2 = ValueOf(loadedD2Opt);
    CHECK(loadedD2.c.Value() == c2);
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

    auto loadedAOpt = dm.QuerySingle<ChainA>(a.id.Value());
    auto& loadedA = ValueOf(loadedAOpt);
    dm.LoadRelations(loadedA);
    REQUIRE(loadedA.bs.All().size() == 1);
    CHECK(loadedA.bs.At(0).label.Value() == "b");
}

TEST_CASE_METHOD(SqlTestFixture, "LoadRelations fills a non-nullable BelongsTo", "[DataMapper][relations][LoadRelations]")
{
    // Regression guard. LoadBelongsTo() returns std::optional<ReferencedRecord>, and BelongsTo has no
    // operator= accepting one - assigning it directly made LoadRelations() ill-formed for *every*
    // record holding a BelongsTo, nullable or not. It stayed latent because all three pre-existing
    // call sites pass a record with no BelongsTo at all (MisalignedDepartment, Human, Suppliers).
    // The fetched record is now adopted through BelongsTo::AdoptFetchedRecord().
    auto dm = DataMapper {};
    dm.CreateTable<ChainA>();
    dm.CreateTable<ChainB>();
    dm.CreateTable<ChainC>();

    auto a = ChainA { .label = "a" };
    dm.Create(a);
    auto b = ChainB { .label = "b" };
    b.a = a;
    dm.Create(b);
    auto c = ChainC { .label = "c" };
    c.b = b;
    dm.Create(c);

    // ChainB carries both a BelongsTo (upwards) and a HasMany (downwards), so one call exercises both
    // branches of the member enumeration.
    auto loadedBOpt = dm.QuerySingle<ChainB>(b.id.Value());
    auto& loadedB = ValueOf(loadedBOpt);
    dm.LoadRelations(loadedB);

    CHECK(loadedB.cs.All().size() == 1);
    CHECK(loadedB.a.Value() == a.id.Value());

    // The BelongsTo is now loaded, so the referenced record is reachable through it. For a mandatory
    // relationship Record() returns the record by reference and would throw if it were still
    // unloaded, so reaching the label at all is the assertion that the adoption happened.
    CHECK(loadedB.a.Record().label.Value() == "a");

    // ...and adopting a fetched record is not a pending change: the foreign key was already whatever
    // the row said, so the field must not come back marked modified.
    CHECK_FALSE(loadedB.a.IsModified());
}

TEST_CASE_METHOD(SqlTestFixture,
                 "LoadRelations fills a nullable BelongsTo, and leaves an unset one alone",
                 "[DataMapper][relations][LoadRelations][nullable]")
{
    // The nullable half of the same fix. A set foreign key must resolve to its record; an unset one
    // must stay unloaded rather than being cleared or throwing.
    auto dm = DataMapper {};
    dm.CreateTable<OptionalParent>();
    dm.CreateTable<OptionalChild>();

    auto parent = OptionalParent { .name = "parent" };
    dm.Create(parent);

    auto attached = OptionalChild { .name = "attached" };
    attached.parent = parent;
    dm.Create(attached);

    auto orphan = OptionalChild { .name = "orphan" };
    dm.Create(orphan);

    auto loadedAttachedOpt = dm.QuerySingle<OptionalChild>(attached.id.Value());
    auto& loadedAttached = ValueOf(loadedAttachedOpt);
    dm.LoadRelations(loadedAttached);
    CHECK(ValueOf(loadedAttached.parent.Value()) == parent.id.Value());
    // For an optional relationship Record() yields an optional over a reference_wrapper, hence .get().
    CHECK(ValueOf(loadedAttached.parent.Record()).get().name.Value() == "parent");
    CHECK_FALSE(loadedAttached.parent.IsModified());

    // The NULL foreign key has nothing to load. It must remain unset - not resolved to some arbitrary
    // row - and asking for the record must report emptiness rather than throwing, because an unset
    // optional relationship is legitimate rather than an error.
    auto loadedOrphanOpt = dm.QuerySingle<OptionalChild>(orphan.id.Value());
    auto& loadedOrphan = ValueOf(loadedOrphanOpt);
    dm.LoadRelations(loadedOrphan);
    CHECK_FALSE(loadedOrphan.parent.Value().has_value());
    CHECK_FALSE(loadedOrphan.parent.Record().has_value());
}
