// SPDX-License-Identifier: Apache-2.0
//
// `CompositeForeignKey<Connection<...>, ...>` - a foreign key spanning several columns.
//
// Design and rationale: `docs/composite-keys-design.md`.
// Ordering ground truth this builds on: `src/tests/CompositeKeyOrderingTests.cpp`.
//
// The two properties worth stating up front, because they are what the tests below check:
//
//   1. The relation holds no column. Every foreign key column is an ordinary `Field` - one data member
//      per database column - so `RecordColumnCount` is unchanged by adding the relation, and every
//      projection skips it automatically.
//   2. Connections may be written in any order. Values are permuted into the referenced record's
//      member order before binding, because a primary key lookup emits its predicates in that order
//      and binds positionally. Getting this wrong yields a wrong row rather than an error, so it is
//      tested directly.

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <tuple>

using namespace Lightweight;

// clang-cl 22.1.3 miscompiles a record that transitively contains a std::function returning a record
// which references it back - it computes a ~5e14-byte stack frame for any function constructing one and
// faults in the prologue. That is the same defect documented at length in
// src/tests/DataMapper/RelationShapeTests.cpp for a self-referential BelongsTo: eight frames deep, one
// consuming most of the stack, no runtime recursion. The identical code compiles and all 22 cases pass
// under MSVC cl, so the shape is sound and the guard is keyed to the toolchain.
#if defined(__clang__) && defined(_MSC_VER)
    #define LIGHTWEIGHT_COMPOSITE_FK_MISCOMPILED 1
#endif

#if !defined(LIGHTWEIGHT_COMPOSITE_FK_MISCOMPILED)

namespace CompositeFk
{

/// Referenced record with a two-column primary key.
struct Parent
{
    static constexpr std::string_view TableName = "CfkParent";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "part_a" }> partA {};
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "part_b" }> partB {};
    Field<std::optional<SqlAnsiString<20>>, SqlRealName { "caption" }> caption {};
};

/// Referencing record, connections written in the same order the parent declares its key.
struct Child
{
    static constexpr std::string_view TableName = "CfkChild";

    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "id" }> id {};
    Field<int32_t, SqlRealName { "ref_a" }> refA {};
    Field<int32_t, SqlRealName { "ref_b" }> refB {};

    CompositeForeignKey<Connection<&Child::refA, &Parent::partA>, Connection<&Child::refB, &Parent::partB>> parent {};
};

/// The same relation with its connections written in the *opposite* order. Semantically identical -
/// the relation permutes values into parent-member order regardless of how it was declared.
struct ChildDeclaredBackwards
{
    static constexpr std::string_view TableName = "CfkChild";

    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "id" }> id {};
    Field<int32_t, SqlRealName { "ref_a" }> refA {};
    Field<int32_t, SqlRealName { "ref_b" }> refB {};

    CompositeForeignKey<Connection<&ChildDeclaredBackwards::refB, &Parent::partB>,
                        Connection<&ChildDeclaredBackwards::refA, &Parent::partA>>
        parent {};
};

/// A three-column parent, the width that dominates the production schema surveyed in
/// `docs/ddl2cpp-relation-generation.md`.
struct WideParent
{
    static constexpr std::string_view TableName = "CfkWideParent";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "k1" }> k1 {};
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "k2" }> k2 {};
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "k3" }> k3 {};
    Field<std::optional<SqlAnsiString<20>>, SqlRealName { "note" }> note {};
};

/// Three connections, deliberately scrambled relative to the parent's member order.
struct WideChild
{
    static constexpr std::string_view TableName = "CfkWideChild";

    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "id" }> id {};
    Field<int32_t, SqlRealName { "a" }> a {};
    Field<int32_t, SqlRealName { "b" }> b {};
    Field<int32_t, SqlRealName { "c" }> c {};

    CompositeForeignKey<Connection<&WideChild::c, &WideParent::k3>,
                        Connection<&WideChild::a, &WideParent::k1>,
                        Connection<&WideChild::b, &WideParent::k2>>
        parent {};
};

} // namespace CompositeFk

using CompositeFk::Child;
using CompositeFk::ChildDeclaredBackwards;
using CompositeFk::Parent;
using CompositeFk::WideChild;
using CompositeFk::WideParent;

// Hoisted to namespace scope: STATIC_CHECK expands its argument inside a macro, where a
// function-local `using` declared in the same statement is not yet visible.
using ChildRelation = decltype(Child::parent);
using BackwardsRelation = decltype(ChildDeclaredBackwards::parent);
using WideRelation = decltype(WideChild::parent);

// ================================================================================================
// Step 1: type-level derivation
// ================================================================================================

TEST_CASE("CompositeForeignKey derives both records from its connections", "[CompositeForeignKey]")
{
    // Neither record is named in the declaration - both are computed from the member pointers, so they
    // cannot disagree with the connections.
    STATIC_CHECK(std::same_as<ChildRelation::Child, Child>);
    STATIC_CHECK(std::same_as<ChildRelation::ReferencedRecord, Parent>);
    STATIC_CHECK(ChildRelation::Count == 2);

    // Three columns works the same way.
    STATIC_CHECK(std::same_as<WideRelation::ReferencedRecord, WideParent>);
    STATIC_CHECK(WideRelation::Count == 3);
}

TEST_CASE("Connection exposes both endpoints and the referenced member index", "[CompositeForeignKey]")
{
    using First = Connection<&Child::refA, &Parent::partA>;
    using Second = Connection<&Child::refB, &Parent::partB>;

    STATIC_CHECK(std::same_as<First::FromRecord, Child>);
    STATIC_CHECK(std::same_as<First::IntoRecord, Parent>);

    // The referenced member index is what drives the permutation: partA is member 0 of Parent, partB
    // member 1. Recovered from the pointer, not restated.
    STATIC_CHECK(First::IntoMemberIndex == 0);
    STATIC_CHECK(Second::IntoMemberIndex == 1);
}

TEST_CASE("CompositeForeignKey is not a column member", "[CompositeForeignKey]")
{
    // The property that keeps every binder, projection and column-offset path untouched: the relation
    // has no Value()/MutableValue()/IsModified(), so it is not FieldWithStorage, therefore not
    // RecordColumnMember - exactly like HasMany and HasOneThrough.
    STATIC_CHECK_FALSE(FieldWithStorage<ChildRelation>);
    STATIC_CHECK_FALSE(RecordColumnMember<ChildRelation>);
    STATIC_CHECK(IsCompositeForeignKey<ChildRelation>);

    // So the record's column count covers exactly its three real columns, and the relation adds none.
    STATIC_CHECK(RecordColumnCount<Child> == 3);
    STATIC_CHECK(RecordMemberCount<Child> == 4); // ...though it is still a member
    STATIC_CHECK(RecordColumnCount<WideChild> == 4);
}

// ================================================================================================
// Step 2: value extraction and ordering
// ================================================================================================

TEST_CASE("ValuesOf reads the foreign key from the record's own fields", "[CompositeForeignKey]")
{
    // The values live in the Field members - one copy each - and are read through the connections'
    // `From` pointers. The relation stores nothing, so nothing can fall out of sync.
    auto child = Child {};
    child.refA = 7;
    child.refB = 9;

    auto const values = ChildRelation::ValuesOf(child);
    CHECK(std::get<0>(values) == 7);
    CHECK(std::get<1>(values) == 9);
}

TEST_CASE("OrderedValuesOf permutes into the referenced record's member order", "[CompositeForeignKey]")
{
    // Declared in parent order already: the permutation is the identity.
    auto child = Child {};
    child.refA = 1;
    child.refB = 2;
    auto const ordered = decltype(Child::parent)::OrderedValuesOf(child);
    CHECK(std::get<0>(ordered) == 1); // partA
    CHECK(std::get<1>(ordered) == 2); // partB

    // Declared backwards: refB->partB is written first, but partB is the parent's *second* member, so
    // the value must still land in slot 1. This is the case that would silently fetch a wrong row if
    // the relation bound values in declaration order.
    auto backwards = ChildDeclaredBackwards {};
    backwards.refA = 1;
    backwards.refB = 2;
    auto const orderedBackwards = decltype(ChildDeclaredBackwards::parent)::OrderedValuesOf(backwards);
    CHECK(std::get<0>(orderedBackwards) == 1); // partA, despite being declared second
    CHECK(std::get<1>(orderedBackwards) == 2); // partB, despite being declared first
}

TEST_CASE("OrderedValuesOf handles a scrambled three-column key", "[CompositeForeignKey]")
{
    // Connections written (c->k3, a->k1, b->k2). The parent declares k1, k2, k3, so the values must
    // come out as (a, b, c) regardless.
    auto child = WideChild {};
    child.a = 10;
    child.b = 20;
    child.c = 30;

    auto const ordered = decltype(WideChild::parent)::OrderedValuesOf(child);
    CHECK(std::get<0>(ordered) == 10); // k1 <- a
    CHECK(std::get<1>(ordered) == 20); // k2 <- b
    CHECK(std::get<2>(ordered) == 30); // k3 <- c
}

// ================================================================================================
// Step 3: navigation surface
// ================================================================================================

TEST_CASE("CompositeForeignKey navigation reports load state", "[CompositeForeignKey]")
{
    auto child = Child {};
    CHECK_FALSE(child.parent.IsLoaded());

    // Emplacing a record marks it loaded and makes it reachable.
    auto parent = std::make_shared<Parent>();
    parent->partA = 1;
    parent->partB = 2;
    parent->caption = SqlAnsiString<20> { "hello" };
    child.parent.EmplaceRecord(parent);

    REQUIRE(child.parent.IsLoaded());
    CHECK(child.parent.Record().partA.Value() == 1);
    CHECK(child.parent->partB.Value() == 2);

    // ...and unloading reverts it.
    child.parent.Unload();
    CHECK_FALSE(child.parent.IsLoaded());
}

// ================================================================================================
// Step 4: loading against a live database
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "CompositeForeignKey loads its referenced record", "[CompositeForeignKey]")
{
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CfkParent" (
                                     "part_a" INT NOT NULL,
                                     "part_b" INT NOT NULL,
                                     "caption" VARCHAR(20) NULL,
                                     PRIMARY KEY ("part_a", "part_b")
                                 ))");
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CfkChild" (
                                     "id" INT NOT NULL PRIMARY KEY,
                                     "ref_a" INT NOT NULL,
                                     "ref_b" INT NOT NULL
                                 ))");

    // Two parents whose key parts are transpositions of each other, so binding them in the wrong order
    // reaches the wrong row rather than failing.
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CfkParent" VALUES (1, 2, 'one-two'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CfkParent" VALUES (2, 1, 'two-one'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CfkChild" VALUES (100, 1, 2))");

    auto dm = DataMapper {};

    auto child = dm.QuerySingle<Child>(100);
    REQUIRE(child.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - guarded above
    CHECK(child->refA.Value() == 1);
    CHECK(child->refB.Value() == 2);

    // QuerySingle(primaryKeys...) does not install auto-loaders - only the query-builder overloads do -
    // so the relation is filled explicitly here. That is the eager path LoadRelations() serves.
    CHECK_FALSE(child->parent.IsLoaded());
    dm.LoadRelations(*child);
    REQUIRE(child->parent.IsLoaded());

    // It resolves to (part_a=1, part_b=2), not to the transposed row.
    CHECK(child->parent.Record().partA.Value() == 1);
    CHECK(child->parent.Record().partB.Value() == 2);
    REQUIRE(child->parent.Record().caption.Value().has_value());
    CHECK(child->parent.Record().caption.Value().value() == "one-two");
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE_METHOD(SqlTestFixture,
                 "CompositeForeignKey declared out of order still loads the right record",
                 "[CompositeForeignKey]")
{
    // The regression this design exists to prevent. `ChildDeclaredBackwards` maps the same table with
    // its connections reversed; without the permutation it would bind (2, 1) and fetch 'two-one'.
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CfkParent" (
                                     "part_a" INT NOT NULL,
                                     "part_b" INT NOT NULL,
                                     "caption" VARCHAR(20) NULL,
                                     PRIMARY KEY ("part_a", "part_b")
                                 ))");
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CfkChild" (
                                     "id" INT NOT NULL PRIMARY KEY,
                                     "ref_a" INT NOT NULL,
                                     "ref_b" INT NOT NULL
                                 ))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CfkParent" VALUES (1, 2, 'one-two'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CfkParent" VALUES (2, 1, 'two-one'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CfkChild" VALUES (100, 1, 2))");

    auto dm = DataMapper {};

    auto child = dm.QuerySingle<ChildDeclaredBackwards>(100);
    REQUIRE(child.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - guarded above
    dm.LoadRelations(*child);
    REQUIRE(child->parent.Record().caption.Value().has_value());
    CHECK(child->parent.Record().caption.Value().value() == "one-two"); // not 'two-one'
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// ================================================================================================
// Step 5: reflected identity, additive
// ================================================================================================

TEST_CASE("RecordPrimaryKeyTuple covers every primary key member", "[CompositeForeignKey][identity]")
{
    // The single-key helpers resolve one member index, so they see only the first key of a composite
    // record. These are added alongside rather than replacing them, so nothing existing changes
    // behaviour.
    STATIC_CHECK(RecordPrimaryKeyCount<Parent> == 2);
    STATIC_CHECK(HasCompositePrimaryKey<Parent>);
    using ParentKeyTuple = std::tuple<int32_t, int32_t>;
    STATIC_CHECK(std::same_as<RecordPrimaryKeyTuple<Parent>, ParentKeyTuple>);

    STATIC_CHECK(RecordPrimaryKeyCount<WideParent> == 3);
    using WideKeyTuple = std::tuple<int32_t, int32_t, int32_t>;
    STATIC_CHECK(std::same_as<RecordPrimaryKeyTuple<WideParent>, WideKeyTuple>);

    // A single-key record yields a one-element tuple and is not composite, so callers can treat both
    // uniformly without special-casing.
    STATIC_CHECK(RecordPrimaryKeyCount<Child> == 1);
    STATIC_CHECK_FALSE(HasCompositePrimaryKey<Child>);
    using ChildKeyTuple = std::tuple<int32_t>;
    STATIC_CHECK(std::same_as<RecordPrimaryKeyTuple<Child>, ChildKeyTuple>);

    // ...and the existing single-key helper is untouched.
    STATIC_CHECK(std::same_as<RecordPrimaryKeyType<Parent>, int32_t>);
}

TEST_CASE("GetPrimaryKeyFields reads every key value in member order", "[CompositeForeignKey][identity]")
{
    auto parent = Parent {};
    parent.partA = 4;
    parent.partB = 5;

    auto const keys = GetPrimaryKeyFields(parent);
    CHECK(std::get<0>(keys) == 4);
    CHECK(std::get<1>(keys) == 5);

    // Three columns, and distinct values so a mis-ordered read would be visible.
    auto wide = WideParent {};
    wide.k1 = 10;
    wide.k2 = 20;
    wide.k3 = 30;
    auto const wideKeys = GetPrimaryKeyFields(wide);
    CHECK(std::get<0>(wideKeys) == 10);
    CHECK(std::get<1>(wideKeys) == 20);
    CHECK(std::get<2>(wideKeys) == 30);

    // The tuple order matches what QuerySingle binds, so it can be applied directly.
    auto const single = GetPrimaryKeyFields(Child {});
    STATIC_CHECK(std::tuple_size_v<decltype(single)> == 1);
}

TEST_CASE_METHOD(SqlTestFixture, "CompositeForeignKey loads across three columns", "[CompositeForeignKey]")
{
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CfkWideParent" (
                                     "k1" INT NOT NULL, "k2" INT NOT NULL, "k3" INT NOT NULL,
                                     "note" VARCHAR(20) NULL,
                                     PRIMARY KEY ("k1", "k2", "k3")
                                 ))");
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CfkWideChild" (
                                     "id" INT NOT NULL PRIMARY KEY,
                                     "a" INT NOT NULL, "b" INT NOT NULL, "c" INT NOT NULL
                                 ))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CfkWideParent" VALUES (10, 20, 30, 'target'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CfkWideParent" VALUES (30, 20, 10, 'decoy'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CfkWideChild" VALUES (1, 10, 20, 30))");

    auto dm = DataMapper {};

    auto child = dm.QuerySingle<WideChild>(1);
    REQUIRE(child.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - guarded above
    dm.LoadRelations(*child);
    REQUIRE(child->parent.Record().note.Value().has_value());
    CHECK(child->parent.Record().note.Value().value() == "target"); // not the reversed decoy
    // NOLINTEND(bugprone-unchecked-optional-access)
}

#else // LIGHTWEIGHT_COMPOSITE_FK_MISCOMPILED

TEST_CASE("CompositeForeignKey is miscompiled by clang-cl", "[CompositeForeignKey][!shouldfail]")
{
    // Placeholder keeping the skipped coverage visible on the one toolchain that cannot compile it.
    // `[!shouldfail]` inverts the result, so this passes the run *because* it fails - and starts failing
    // the moment the guard is removed without the coverage coming back.
    FAIL("clang-cl 22.1.3 computes an impossible stack frame for a record holding a CompositeForeignKey "
         "and faults in the prologue. These tests are compiled out here but all pass under MSVC cl. See "
         "the comment above the guard.");
}

#endif // LIGHTWEIGHT_COMPOSITE_FK_MISCOMPILED
