// SPDX-License-Identifier: Apache-2.0
//
// Composite keys, end to end: raw SQL DDL declaring them, through to the record mapping.
//
// A composite foreign key references a composite primary key, so both sides need the same notion of
// "a key spans several columns". Under the guiding principle that every database column is one data
// member, that means several members marked `PrimaryKey` on the referenced side, and several ordinary
// column members plus one `CompositeForeignKey` relation on the referencing side.
//
// Where each layer stands:
//
//   - **Schema reading**: reports both column lists, in order. Never needed changing.
//   - **Row lookup**: `QuerySingle`/`Update`/`Delete` emit one `WHERE` per primary-key field and bind
//     one argument each, so fetching by a whole composite key already worked.
//   - **Reflected identity**: `RecordPrimaryKeyType` / `GetPrimaryKeyField()` still resolve a *single*
//     member index, deliberately - `RecordPrimaryKeyTuple` / `GetPrimaryKeyFields()` were added beside
//     them so no existing caller changes shape.
//   - **Relations**: spelled as `CompositeForeignKey<Connection<...>, ...>`. `BelongsTo` was left
//     alone; see `docs/composite-keys-design.md` for why.
//
// Behavioural coverage of the relation itself lives in `CompositeForeignKeyTests.cpp`; the predicate
// ordering it depends on is pinned in `CompositeKeyOrderingTests.cpp`. This file covers the DDL-facing
// end: what the schema reader sees, and that a composite-key table still maps as a plain record.
//
// The shapes come from a production MS SQL Server schema surveyed for
// `docs/ddl2cpp-relation-generation.md` (686 tables, 1860 foreign keys, 170 composite primary keys and
// 90 composite foreign keys concentrated on 21 parent tables). The table and column names here are
// deliberately generic and carry no resemblance to it - only the shapes are reproduced.
//
// The DDL is raw SQL rather than `CreateTable<>()` so the C++ mapping is tested against
// independently-declared tables rather than against its own generator.

#include "Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <variant>

using namespace Lightweight;

// ================================================================================================
// Composite PRIMARY key
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "Composite primary key is read back from the schema", "[CompositeKey][SqlSchema]")
{
    // The schema reader handles this correctly - it is only the record mapping that cannot express
    // it. Asserting that here pins the boundary: whatever a fix does, it does not need to change
    // schema reading.
    auto stmt = SqlStatement {};

    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CkComposite" (
                                     "tenant_id" INT NOT NULL,
                                     "entry_no" INT NOT NULL,
                                     "label" VARCHAR(40) NULL,
                                     PRIMARY KEY ("tenant_id", "entry_no")
                                 ))");

    auto const tables = SqlSchema::ReadAllTables(stmt, stmt.Connection().DatabaseName(), /*schema=*/"");
    auto const table = std::ranges::find_if(tables, [](SqlSchema::Table const& t) { return t.name == "CkComposite"; });
    REQUIRE(table != tables.end());

    // Both key columns are reported, in declaration order.
    REQUIRE(table->primaryKeys.size() == 2);
    CHECK(table->primaryKeys[0] == "tenant_id");
    CHECK(table->primaryKeys[1] == "entry_no");

    // ...and both are flagged on the columns themselves.
    auto const primaryKeyColumns = std::ranges::count_if(table->columns, [](auto const& c) { return c.isPrimaryKey; });
    CHECK(primaryKeyColumns == 2);
}

namespace CompositeKeyGap
{

/// A record whose identity is a pair of columns, i.e. the C++ counterpart of
/// `PRIMARY KEY ("tenant_id", "entry_no")`.
///
/// Both members are declared as primary keys, which is the only spelling available. It compiles, but
/// see the test below for what it actually means.
struct CkCompositeRecord
{
    static constexpr std::string_view TableName = "CkComposite";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "tenant_id" }> tenantId {};
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "entry_no" }> entryNo {};
    Field<std::optional<SqlAnsiString<40>>, SqlRealName { "label" }> label {};
};

/// The referenced side of the composite foreign key declared in the DDL above.
struct CkParentRecord
{
    static constexpr std::string_view TableName = "CkParent";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "part_a" }> partA {};
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "part_b" }> partB {};
    Field<std::optional<SqlAnsiString<40>>, SqlRealName { "caption" }> caption {};
};

/// The referencing side: two ordinary column members, one per column.
struct CkChildRecord
{
    static constexpr std::string_view TableName = "CkChild";

    Field<int32_t, PrimaryKey::AutoAssign, SqlRealName { "id" }> id {};
    Field<int32_t, SqlRealName { "ref_a" }> refA {};
    Field<int32_t, SqlRealName { "ref_b" }> refB {};
};

} // namespace CompositeKeyGap

using CompositeKeyGap::CkCompositeRecord;

TEST_CASE("Reflected identity covers every primary key member", "[CompositeKey][identity]")
{
    // Previously a gap: `RecordPrimaryKeyType` resolves through `RecordPrimaryKeyIndex`, a single member
    // index, so anything built on it sees only the first key field. That helper is unchanged - it still
    // names one field, and existing single-key callers are unaffected - but the composite-aware pair
    // added beside it reports the whole key.
    STATIC_CHECK(RecordPrimaryKeyCount<CkCompositeRecord> == 2);
    STATIC_CHECK(HasCompositePrimaryKey<CkCompositeRecord>);
    using CompositeKeyTuple = std::tuple<int32_t, int32_t>;
    STATIC_CHECK(std::same_as<RecordPrimaryKeyTuple<CkCompositeRecord>, CompositeKeyTuple>);

    // The single-key helper still collapses to the first field, deliberately: making it a tuple would
    // change the shape of every existing caller, including CreateExplicit's return type.
    STATIC_CHECK(std::same_as<RecordPrimaryKeyType<CkCompositeRecord>, int32_t>);
}

TEST_CASE("GetPrimaryKeyField returns the first primary key field, not the last", "[CompositeKey][identity]")
{
    // Previously a gap: the loop kept overwriting its result for every matching-type primary key
    // member, so it silently returned the LAST one rather than the first - contradicting both its own
    // inline comment and its doc comment. tenantId and entryNo share the same value type (int32_t), so
    // this is exactly the shape that triggers it.
    auto record = CkCompositeRecord {};
    record.tenantId = 7;
    record.entryNo = 3;

    CHECK(GetPrimaryKeyField(record) == 7); // tenantId, declared first - not entryNo
}

TEST_CASE_METHOD(SqlTestFixture, "A composite primary key does identify a row", "[CompositeKey]")
{
    // Surprisingly, this half already works. QuerySingle() emits one `WHERE` per primary-key field
    // (it enumerates members and adds a predicate for each `FieldType::IsPrimaryKey`) and binds one
    // argument per placeholder, so passing the whole key selects exactly one row.
    //
    // So the composite-key gap is *not* in row lookup. It is in the two places that assume a single
    // key field: `RecordPrimaryKeyType` / `GetPrimaryKeyField()` (which collapse to the first one),
    // and `BelongsTo`, which can only name one referenced field. Pinned here so a composite-key
    // implementation does not regress what already works.
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CkComposite" (
                                     "tenant_id" INT NOT NULL,
                                     "entry_no" INT NOT NULL,
                                     "label" VARCHAR(40) NULL,
                                     PRIMARY KEY ("tenant_id", "entry_no")
                                 ))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CkComposite" VALUES (1, 1, 'first'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CkComposite" VALUES (1, 2, 'second'))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CkComposite" VALUES (2, 1, 'third'))");

    auto dm = DataMapper {};

    // Both key parts are bound, and the right row of the three comes back.
    auto const second = dm.QuerySingle<CkCompositeRecord>(1, 2);
    REQUIRE(second.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - guarded above
    CHECK(second->tenantId.Value() == 1);
    CHECK(second->entryNo.Value() == 2);
    REQUIRE(second->label.Value().has_value());
    CHECK(second->label.Value().value() == "second");

    // ...and the row sharing entry_no but not tenant_id is distinguished too.
    auto const third = dm.QuerySingle<CkCompositeRecord>(2, 1);
    REQUIRE(third.has_value());
    REQUIRE(third->label.Value().has_value());
    CHECK(third->label.Value().value() == "third");
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// ================================================================================================
// Composite FOREIGN key
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "Composite foreign key is read back from the schema", "[CompositeKey][SqlSchema]")
{
    // As with the primary key: the reader is fine, the record mapping is not.
    auto stmt = SqlStatement {};

    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CkParent" (
                                     "part_a" INT NOT NULL,
                                     "part_b" INT NOT NULL,
                                     "caption" VARCHAR(40) NULL,
                                     PRIMARY KEY ("part_a", "part_b")
                                 ))");
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CkChild" (
                                     "id" INT NOT NULL PRIMARY KEY,
                                     "ref_a" INT NOT NULL,
                                     "ref_b" INT NOT NULL,
                                     CONSTRAINT "FK_CkChild_CkParent"
                                         FOREIGN KEY ("ref_a", "ref_b") REFERENCES "CkParent" ("part_a", "part_b")
                                 ))");

    auto const tables = SqlSchema::ReadAllTables(stmt, stmt.Connection().DatabaseName(), /*schema=*/"");
    auto const child = std::ranges::find_if(tables, [](SqlSchema::Table const& t) { return t.name == "CkChild"; });
    REQUIRE(child != tables.end());
    REQUIRE(child->foreignKeys.size() == 1);

    // Both column pairs are reported, and the ordering pairs ref_a->part_a, ref_b->part_b.
    auto const& constraint = child->foreignKeys.front();
    REQUIRE(constraint.foreignKey.columns.size() == 2);
    REQUIRE(constraint.primaryKey.columns.size() == 2);
    CHECK(constraint.foreignKey.columns[0] == "ref_a");
    CHECK(constraint.foreignKey.columns[1] == "ref_b");
    CHECK(constraint.primaryKey.columns[0] == "part_a");
    CHECK(constraint.primaryKey.columns[1] == "part_b");
    CHECK(constraint.primaryKey.table.table == "CkParent");
}

TEST_CASE("A composite foreign key is expressed as a CompositeForeignKey", "[CompositeKey]")
{
    // Previously a gap: `BelongsTo<&Parent::field>` names one referenced field, so a two-column
    // reference could not be written at all, and ddl2cpp reported such keys under "Foreign keys
    // ignored".
    //
    // It is now spelled as a list of connections, each pairing one of this record's columns with the
    // column it references - see `CompositeForeignKeyTests.cpp` for the behavioural coverage and
    // `docs/composite-keys-design.md` for why the pairing lives in the type rather than in two
    // positionally-matched lists.
    //
    // `BelongsTo` was deliberately left alone: it is inseparably a *column* (storage, one bound ODBC
    // index) as well as a navigator, and widening it would have broken the one-member-one-column
    // invariant that every projection and column-offset path depends on.
    using Relation = CompositeForeignKey<
        Connection<Member(CompositeKeyGap::CkChildRecord::refA), Member(CompositeKeyGap::CkParentRecord::partA)>,
        Connection<Member(CompositeKeyGap::CkChildRecord::refB), Member(CompositeKeyGap::CkParentRecord::partB)>>;

    STATIC_CHECK(Relation::Count == 2);
    STATIC_CHECK(std::same_as<Relation::ReferencedRecord, CompositeKeyGap::CkParentRecord>);

    // It carries no column of its own: the two foreign key columns are ordinary Fields on the record.
    STATIC_CHECK_FALSE(RecordColumnMember<Relation>);
}

// ================================================================================================
// What works today, and is worth keeping working
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "A composite-key table still maps as a plain record", "[CompositeKey][SqlSchema]")
{
    // The fallback that makes the gap survivable: the columns are ordinary, so the data round-trips.
    // Only identity and relations are affected. A fix must not regress this.
    auto stmt = SqlStatement {};
    (void) stmt.ExecuteDirect(R"(CREATE TABLE "CkComposite" (
                                     "tenant_id" INT NOT NULL,
                                     "entry_no" INT NOT NULL,
                                     "label" VARCHAR(40) NULL,
                                     PRIMARY KEY ("tenant_id", "entry_no")
                                 ))");
    (void) stmt.ExecuteDirect(R"(INSERT INTO "CkComposite" VALUES (7, 3, 'kept'))");

    auto dm = DataMapper {};
    auto const all = dm.Query<CkCompositeRecord>().All();
    REQUIRE(all.size() == 1);
    CHECK(all[0].tenantId.Value() == 7);
    CHECK(all[0].entryNo.Value() == 3);
    REQUIRE(all[0].label.Value().has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - guarded above
    CHECK(all[0].label.Value().value() == "kept");
}

namespace CompositeKeyGap
{

/// Two auto-assigned GUID primary keys - the type `GenerateAutoAssignPrimaryKey`'s <= 1 static_assert
/// exists to reject. See the test below for why counting it correctly matters.
struct GuidMultiPkRecord
{
    Field<SqlGuid, PrimaryKey::AutoAssign> first {};
    Field<SqlGuid, PrimaryKey::AutoAssign> second {};
};

} // namespace CompositeKeyGap

TEST_CASE("AutoAssignPrimaryKeyFieldCount counts auto-assigned GUID keys, not just incrementable ones",
          "[CompositeKey][identity]")
{
    // Previously a gap: the counting concept only recognized `value + 1`-style keys, so a record with
    // two `Field<SqlGuid, PrimaryKey::AutoAssign>` members counted as zero and slipped past
    // GenerateAutoAssignPrimaryKey's `<= 1` static_assert - which exists precisely to reject this shape,
    // since auto-assignment yields one value that SetId() then writes into *every* key member.
    STATIC_CHECK(Lightweight::detail::AutoAssignPrimaryKeyFieldCount<CompositeKeyGap::GuidMultiPkRecord> == 2);
}
