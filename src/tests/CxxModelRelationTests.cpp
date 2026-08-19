// SPDX-License-Identifier: Apache-2.0
//
// Relation generation in ddl2cpp: the inverse and through relations implied by a schema.
//
// `CxxModelPrinterTests.cpp` covers column type mapping and the child-side `BelongsTo`. This file
// covers the other direction - `HasMany`, `HasManyThrough`, `HasOneThrough` - which requires
// schema-wide knowledge because a relation on a parent is implied by a foreign key declared on some
// other table.
//
// The shapes tested here are taken from the reference schema surveyed in
// `docs/ddl2cpp-relation-generation.md` (a large production schema, 686 tables / 1860 foreign keys),
// reduced to the smallest fixtures that reproduce each structural case found there:
//
//   - a plain parent/child pair                          -> HasMany
//   - two foreign keys from one child into one parent     -> HasMany + selector (that schema has a
//                                                            pair joined by 55 foreign keys)
//   - a two-column join table                            -> HasManyThrough on both sides
//   - a join table whose owner key is uniquely indexed    -> HasOneThrough on that owner's side
//   - a uniquely indexed child foreign key                -> one-to-one
//   - a join table carrying payload columns               -> NOT a join table (association object)
//   - composite foreign keys                              -> skipped, as for BelongsTo

#include <Lightweight/Lightweight.hpp>
#include <Lightweight/Tools/CxxModelPrinter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using Lightweight::Tools::CxxModelPrinter;
using Relation = CxxModelPrinter::PlannedRelation;
using Kind = Relation::Kind;

namespace
{

using namespace Lightweight::SqlColumnTypeDefinitions;

/// Builds a fully qualified name in the default (empty) catalog and schema, as ReadAllTables yields
/// for a single-schema database.
///
/// @param table Table name.
/// @return The qualified name.
Lightweight::SqlSchema::FullyQualifiedTableName Qualified(std::string_view table)
{
    return { .catalog = "", .schema = "", .table = std::string { table } };
}

/// Builds a single-column foreign key constraint.
///
/// @param childTable Table holding the foreign key.
/// @param childColumn Foreign key column.
/// @param parentTable Referenced table.
/// @param parentColumn Referenced column, its primary key.
/// @return The constraint.
Lightweight::SqlSchema::ForeignKeyConstraint ForeignKey(std::string_view childTable,
                                                        std::string_view childColumn,
                                                        std::string_view parentTable,
                                                        std::string_view parentColumn = "id")
{
    return { .foreignKey = { .table = Qualified(childTable), .columns = { std::string { childColumn } } },
             .primaryKey = { .table = Qualified(parentTable), .columns = { std::string { parentColumn } } } };
}

/// @return The relations planned for @p table, or an empty vector if none.
std::vector<Relation> RelationsOn(CxxModelPrinter::RelationPlan const& plan, std::string_view table)
{
    auto const it = plan.find(std::string { table });
    return it != plan.end() ? it->second : std::vector<Relation> {};
}

/// @return The single relation planned for @p table; fails the test if there is not exactly one.
Relation SoleRelationOn(CxxModelPrinter::RelationPlan const& plan, std::string_view table)
{
    auto const relations = RelationsOn(plan, table);
    REQUIRE(relations.size() == 1);
    return relations.front();
}

Lightweight::SqlSchema::Column IdColumn()
{
    return { .name = "id", .type = Integer {}, .isNullable = false, .isPrimaryKey = true };
}

Lightweight::SqlSchema::Column ForeignKeyColumn(std::string_view name, bool isUnique = false)
{
    return {
        .name = std::string { name }, .type = Integer {}, .isNullable = false, .isUnique = isUnique, .isForeignKey = true
    };
}

} // namespace

// ================================================================================================
// HasMany - the inverse of a plain child foreign key
// ================================================================================================

TEST_CASE("PlanRelations: a child foreign key yields a HasMany on the parent", "[CxxModelPrinter][relations]")
{
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "author", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "book",
          .columns = { IdColumn(), ForeignKeyColumn("author_id") },
          .foreignKeys = { ForeignKey("book", "author_id", "author") },
          .primaryKeys = { "id" } },
    };

    auto const plan = CxxModelPrinter::PlanRelations(tables);

    // The relation lands on the parent, not the child - the child already has its BelongsTo.
    auto const relation = SoleRelationOn(plan, "author");
    CHECK(relation.kind == Kind::HasMany);
    CHECK(relation.ownerTable == "author");
    CHECK(relation.referencedTable == "book");
    CHECK(relation.ownerForeignKeyColumn == "author_id");
    CHECK(relation.throughTable.empty());

    // A single foreign key between the pair is unambiguous, so no selector is needed.
    CHECK_FALSE(relation.ownerSelectorRequired);

    // ...and nothing is planned on the child.
    CHECK(RelationsOn(plan, "book").empty());
}

TEST_CASE("PlanRelations: two foreign keys into one parent require selectors", "[CxxModelPrinter][relations]")
{
    // The shape the reference schema has in abundance - one pair there is joined by 55 foreign keys.
    // Without a selector `HasMany<Meeting>` cannot resolve its inverse and is a compile error, so the
    // selector is what makes such a schema generatable at all.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "person", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "meeting",
          .columns = { IdColumn(), ForeignKeyColumn("organizer_id"), ForeignKeyColumn("minute_taker_id") },
          .foreignKeys = { ForeignKey("meeting", "organizer_id", "person"),
                           ForeignKey("meeting", "minute_taker_id", "person") },
          .primaryKeys = { "id" } },
    };

    auto const plan = CxxModelPrinter::PlanRelations(tables);
    auto const relations = RelationsOn(plan, "person");
    REQUIRE(relations.size() == 2);

    for (auto const& relation: relations)
    {
        CHECK(relation.kind == Kind::HasMany);
        CHECK(relation.referencedTable == "meeting");
        CHECK(relation.ownerSelectorRequired); // both are ambiguous without one
    }

    // Each names its own foreign key column...
    CHECK(relations[0].ownerForeignKeyColumn != relations[1].ownerForeignKeyColumn);

    // ...and the member names must differ too, or the generated struct would not compile.
    CHECK(relations[0].memberName != relations[1].memberName);
    CHECK(relations[0].memberName.contains(relations[0].ownerForeignKeyColumn));
}

TEST_CASE("PlanRelations: a uniquely indexed child foreign key is one-to-one", "[CxxModelPrinter][relations]")
{
    // A unique index over the child's foreign key means at most one child per parent, so the relation
    // is scalar rather than a collection.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "user", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "profile",
          .columns = { IdColumn(), ForeignKeyColumn("user_id", /*isUnique=*/true) },
          .foreignKeys = { ForeignKey("profile", "user_id", "user") },
          .primaryKeys = { "id" } },
    };

    auto const relation = SoleRelationOn(CxxModelPrinter::PlanRelations(tables), "user");
    CHECK(relation.kind == Kind::HasOne);
    CHECK(relation.referencedTable == "profile");
}

TEST_CASE("PlanRelations: uniqueness via a single-column unique index, not just the column flag",
          "[CxxModelPrinter][relations]")
{
    // The same one-to-one shape, but declared as an index rather than a column-level UNIQUE. Both
    // spellings occur in real schemas and must be treated the same.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "user", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "profile",
          .columns = { IdColumn(), ForeignKeyColumn("user_id") },
          .foreignKeys = { ForeignKey("profile", "user_id", "user") },
          .primaryKeys = { "id" },
          .indexes = { { .name = "UX_profile_user", .columns = { "user_id" }, .isUnique = true } } },
    };

    CHECK(SoleRelationOn(CxxModelPrinter::PlanRelations(tables), "user").kind == Kind::HasOne);
}

TEST_CASE("PlanRelations: a composite unique index does not make a relation one-to-one", "[CxxModelPrinter][relations]")
{
    // Uniqueness of (user_id, kind) says nothing about user_id alone, so the relation stays a
    // collection. Treating it as scalar would silently drop rows.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "user", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "profile",
          .columns = { IdColumn(), ForeignKeyColumn("user_id"), { .name = "kind", .type = Integer {} } },
          .foreignKeys = { ForeignKey("profile", "user_id", "user") },
          .primaryKeys = { "id" },
          .indexes = { { .name = "UX_profile_user_kind", .columns = { "user_id", "kind" }, .isUnique = true } } },
    };

    CHECK(SoleRelationOn(CxxModelPrinter::PlanRelations(tables), "user").kind == Kind::HasMany);
}

// ================================================================================================
// HasManyThrough / HasOneThrough - across a join table
// ================================================================================================

TEST_CASE("PlanRelations: a two-column join table yields HasManyThrough on both sides", "[CxxModelPrinter][relations]")
{
    // A join table with a key of its own, so both foreign key columns become BelongsTo members and the
    // through-relation can actually resolve them. (A join table keyed on its own foreign keys is
    // skipped instead - see the composite-key test below.)
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "project", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "", .name = "user", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "project_user",
          .columns = { IdColumn(), ForeignKeyColumn("project_id"), ForeignKeyColumn("user_id") },
          .foreignKeys = { ForeignKey("project_user", "project_id", "project"),
                           ForeignKey("project_user", "user_id", "user") },
          .primaryKeys = { "id" } },
    };

    auto const plan = CxxModelPrinter::PlanRelations(tables);

    // Each side reaches the *other* side, through the join table.
    auto const fromProject = SoleRelationOn(plan, "project");
    CHECK(fromProject.kind == Kind::HasManyThrough);
    CHECK(fromProject.referencedTable == "user");
    CHECK(fromProject.throughTable == "project_user");
    CHECK(fromProject.ownerForeignKeyColumn == "project_id");
    CHECK(fromProject.referencedForeignKeyColumn == "user_id");

    auto const fromUser = SoleRelationOn(plan, "user");
    CHECK(fromUser.kind == Kind::HasManyThrough);
    CHECK(fromUser.referencedTable == "project");
    CHECK(fromUser.throughTable == "project_user");
    CHECK(fromUser.ownerForeignKeyColumn == "user_id");
    CHECK(fromUser.referencedForeignKeyColumn == "project_id");

    // The join table itself gets no inverse relation: it is consumed by the through relations, and a
    // HasMany onto it as well would be redundant.
    CHECK(RelationsOn(plan, "project_user").empty());
}

TEST_CASE("PlanRelations: a join table with a uniquely indexed owner key yields HasOneThrough",
          "[CxxModelPrinter][relations]")
{
    // When the join record's own foreign key back to an owner is unique, that owner reaches at most
    // one join row and therefore at most one far record. `locker_id` being unique means each locker is
    // linked to at most one employee - a one-to-one from locker's side - while `employee_id` staying
    // unconstrained means one employee can still have several lockers.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "employee", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "", .name = "locker", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "employee_locker",
          .columns = { IdColumn(), ForeignKeyColumn("employee_id"), ForeignKeyColumn("locker_id", /*isUnique=*/true) },
          .foreignKeys = { ForeignKey("employee_locker", "employee_id", "employee"),
                           ForeignKey("employee_locker", "locker_id", "locker") },
          .primaryKeys = { "id" } },
    };

    auto const plan = CxxModelPrinter::PlanRelations(tables);

    // employee_id is not uniquely indexed, so one employee can have several join rows: a collection.
    auto const fromEmployee = SoleRelationOn(plan, "employee");
    CHECK(fromEmployee.kind == Kind::HasManyThrough);
    CHECK(fromEmployee.referencedTable == "locker");

    // locker_id is uniquely indexed, so each locker reaches at most one join row, hence one employee.
    auto const fromLocker = SoleRelationOn(plan, "locker");
    CHECK(fromLocker.kind == Kind::HasOneThrough);
    CHECK(fromLocker.referencedTable == "employee");
}

TEST_CASE("PlanRelations: a join table keyed on its own foreign keys yields no through-relation",
          "[CxxModelPrinter][relations]")
{
    // The classic composite-key join table: its primary key *is* the two foreign keys. ddl2cpp emits a
    // column that is both a primary key and a foreign key as a plain Field rather than a BelongsTo, and
    // a through-relation can only resolve its two sides through BelongsTo members - so planning one here
    // would generate a record that does not compile (#556). Chinook's PlaylistTrack is exactly this
    // shape, and the relations it used to imply were unusable: relation auto-loading never reached them
    // only because the generated Description<> omitted relation members altogether.
    //
    // Supporting this shape needs BelongsTo to be usable as a primary key, which it is not today
    // (BelongsTo::IsPrimaryKey is hard-coded false). Until then, generate nothing rather than something
    // broken. A join table with a key of its own keeps working - see the HasManyThrough test above.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "playlist", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "", .name = "track", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "playlist_track",
          .columns = { ForeignKeyColumn("playlist_id"), ForeignKeyColumn("track_id") },
          .foreignKeys = { ForeignKey("playlist_track", "playlist_id", "playlist"),
                           ForeignKey("playlist_track", "track_id", "track") },
          .primaryKeys = { "playlist_id", "track_id" } },
    };

    auto const plan = CxxModelPrinter::PlanRelations(tables);

    CHECK(RelationsOn(plan, "playlist").empty());
    CHECK(RelationsOn(plan, "track").empty());
    CHECK(RelationsOn(plan, "playlist_track").empty());
}

TEST_CASE("PlanRelations: a self-reference declared before its primary key yields no relation",
          "[CxxModelPrinter][relations]")
{
    // The same shape CxxModelPrinterTests pins on the column side ("self-reference declared before its
    // primary key stays a plain field"): a pointer-to-member may only name a member the compiler has
    // already seen, so this foreign key falls back to a plain Field instead of a BelongsTo. HasMany
    // resolves its inverse through exactly that BelongsTo, so planning one would emit a record whose
    // ConfigureRelationAutoLoading fails to compile - now that Description<> lists relation members and
    // the loader is actually instantiated (#556).
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "",
          .name = "node",
          .columns = { ForeignKeyColumn("parent_id"), IdColumn() },
          .foreignKeys = { ForeignKey("node", "parent_id", "node") },
          .primaryKeys = { "id" } },
    };

    CHECK(RelationsOn(CxxModelPrinter::PlanRelations(tables), "node").empty());
}

TEST_CASE("PlanRelations: a self-reference into a non-primary-key column yields no relation", "[CxxModelPrinter][relations]")
{
    // PostgreSQL and SQL Server allow a foreign key to target any UNIQUE NOT NULL column, but BelongsTo
    // static_asserts that the member it points at is a primary key - so the column stays a plain Field
    // and, for the same reason as above, implies no inverse relation either.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "",
          .name = "doc",
          .columns = { IdColumn(),
                       Lightweight::SqlSchema::Column { .name = "code", .type = Integer {}, .isNullable = false },
                       ForeignKeyColumn("parent_code") },
          .foreignKeys = { ForeignKey("doc", "parent_code", "doc", "code") },
          .primaryKeys = { "id" } },
    };

    CHECK(RelationsOn(CxxModelPrinter::PlanRelations(tables), "doc").empty());
}

TEST_CASE("PlanRelations: a join table with payload columns stays an entity", "[CxxModelPrinter][relations]")
{
    // The association-object shape: the join table carries data of its own, so collapsing it into a
    // HasManyThrough would hide that column. It must instead behave like any other child table -
    // a HasMany on each side - so the payload stays reachable.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "item", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "", .name = "keyword", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "item_keyword",
          .columns = { IdColumn(),
                       ForeignKeyColumn("item_id"),
                       ForeignKeyColumn("keyword_id"),
                       { .name = "weight", .type = Integer {} } }, // <-- payload
          .foreignKeys = { ForeignKey("item_keyword", "item_id", "item"),
                           ForeignKey("item_keyword", "keyword_id", "keyword") },
          .primaryKeys = { "id" } },
    };

    auto const plan = CxxModelPrinter::PlanRelations(tables);

    // Not through-relations: each side sees the join record itself.
    auto const fromItem = SoleRelationOn(plan, "item");
    CHECK(fromItem.kind == Kind::HasMany);
    CHECK(fromItem.referencedTable == "item_keyword");
    CHECK(fromItem.throughTable.empty());

    auto const fromKeyword = SoleRelationOn(plan, "keyword");
    CHECK(fromKeyword.kind == Kind::HasMany);
    CHECK(fromKeyword.referencedTable == "item_keyword");
}

TEST_CASE("PlanRelations: a table with two foreign keys to the same table is not a join table",
          "[CxxModelPrinter][relations]")
{
    // Both keys point at `person`, so there is no far side to hop to. This must stay two HasMany
    // relations on `person`, not a nonsensical person-to-person through relation.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "person", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "marriage",
          .columns = { IdColumn(), ForeignKeyColumn("spouse_a_id"), ForeignKeyColumn("spouse_b_id") },
          .foreignKeys = { ForeignKey("marriage", "spouse_a_id", "person"),
                           ForeignKey("marriage", "spouse_b_id", "person") },
          .primaryKeys = { "id" } },
    };

    auto const relations = RelationsOn(CxxModelPrinter::PlanRelations(tables), "person");
    REQUIRE(relations.size() == 2);
    for (auto const& relation: relations)
    {
        CHECK(relation.kind == Kind::HasMany);
        CHECK(relation.throughTable.empty());
        CHECK(relation.ownerSelectorRequired); // ambiguous: two keys from marriage into person
    }
}

// ================================================================================================
// Limits: what is deliberately not planned
// ================================================================================================

TEST_CASE("PlanRelations: composite foreign keys are skipped", "[CxxModelPrinter][relations]")
{
    // A composite foreign key has no BelongsTo either - BelongsTo names a single referenced field -
    // so there is no inverse to generate. The reference schema has 90 of these.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "",
          .name = "parent",
          .columns = { { .name = "a", .type = Integer {}, .isPrimaryKey = true },
                       { .name = "b", .type = Integer {}, .isPrimaryKey = true } },
          .primaryKeys = { "a", "b" } },
        { .schema = "",
          .name = "child",
          .columns = { IdColumn(), ForeignKeyColumn("ref_a"), ForeignKeyColumn("ref_b") },
          .foreignKeys = { { .foreignKey = { .table = Qualified("child"), .columns = { "ref_a", "ref_b" } },
                             .primaryKey = { .table = Qualified("parent"), .columns = { "a", "b" } } } },
          .primaryKeys = { "id" } },
    };

    CHECK(CxxModelPrinter::PlanRelations(tables).empty());
}

TEST_CASE("PlanRelations: a foreign key to a table outside the set is skipped", "[CxxModelPrinter][relations]")
{
    // Generating a relation onto a record that is not being generated would not compile.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "",
          .name = "book",
          .columns = { IdColumn(), ForeignKeyColumn("author_id") },
          .foreignKeys = { ForeignKey("book", "author_id", "author_not_in_this_set") },
          .primaryKeys = { "id" } },
    };

    CHECK(CxxModelPrinter::PlanRelations(tables).empty());
}

TEST_CASE("PlanRelations: a schema with no foreign keys plans nothing", "[CxxModelPrinter][relations]")
{
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "lonely", .columns = { IdColumn() }, .primaryKeys = { "id" } },
    };

    CHECK(CxxModelPrinter::PlanRelations(tables).empty());
}

// ================================================================================================
// Emission - the generated C++ text
// ================================================================================================

TEST_CASE("CxxModelPrinter: emits HasMany with a forward declaration, not an include", "[CxxModelPrinter][relations]")
{
    // The parent names the child, while the child's BelongsTo names the parent. Including both ways
    // would be a cycle, so the parent forward-declares instead - which is sound because HasMany holds
    // std::vector<std::shared_ptr<Child>> and needs no complete type here.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "author", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "book",
          .columns = { IdColumn(), ForeignKeyColumn("author_id") },
          .foreignKeys = { ForeignKey("book", "author_id", "author") },
          .primaryKeys = { "id" } },
    };

    auto printer = CxxModelPrinter { CxxModelPrinter::Config {} };
    printer.ResolveOrderAndPrintTable(tables);

    auto const authorHeader = printer.HeaderFileForTheTable("Models", "author");
    INFO("author.hpp:\n" << authorHeader);

    CHECK(authorHeader.contains("Light::HasMany<book>"));
    CHECK(authorHeader.contains("struct book;"));                // forward-declared...
    CHECK_FALSE(authorHeader.contains("#include \"book.hpp\"")); // ...never included

    // The child keeps its BelongsTo and does include the parent, as before.
    auto const bookHeader = printer.HeaderFileForTheTable("Models", "book");
    INFO("book.hpp:\n" << bookHeader);
    CHECK(bookHeader.contains("Light::BelongsTo<&author::id"));
    CHECK(bookHeader.contains("#include \"author.hpp\""));
}

TEST_CASE("CxxModelPrinter: emits a selector for each of several foreign keys into one table",
          "[CxxModelPrinter][relations]")
{
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "person", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "meeting",
          .columns = { IdColumn(), ForeignKeyColumn("organizer_id"), ForeignKeyColumn("minute_taker_id") },
          .foreignKeys = { ForeignKey("meeting", "organizer_id", "person"),
                           ForeignKey("meeting", "minute_taker_id", "person") },
          .primaryKeys = { "id" } },
    };

    auto printer = CxxModelPrinter { CxxModelPrinter::Config {} };
    printer.ResolveOrderAndPrintTable(tables);

    auto const header = printer.HeaderFileForTheTable("Models", "person");
    INFO("person.hpp:\n" << header);

    // Both selectors present, so each HasMany resolves to its own foreign key.
    CHECK(header.contains(R"(Light::SqlRealName { "organizer_id" })"));
    CHECK(header.contains(R"(Light::SqlRealName { "minute_taker_id" })"));

    // One include of the child at most, and it is a forward declaration.
    CHECK(header.contains("struct meeting;"));
}

TEST_CASE("CxxModelPrinter: emits HasManyThrough for a join table", "[CxxModelPrinter][relations]")
{
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "project", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "", .name = "user", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "project_user",
          .columns = { IdColumn(), ForeignKeyColumn("project_id"), ForeignKeyColumn("user_id") },
          .foreignKeys = { ForeignKey("project_user", "project_id", "project"),
                           ForeignKey("project_user", "user_id", "user") },
          .primaryKeys = { "id" } },
    };

    auto printer = CxxModelPrinter { CxxModelPrinter::Config {} };
    printer.ResolveOrderAndPrintTable(tables);

    auto const header = printer.HeaderFileForTheTable("Models", "project");
    INFO("project.hpp:\n" << header);

    // The far record first, the join record second - matching HasManyThrough<Referenced, Through<Join>>.
    CHECK(header.contains("Light::HasManyThrough<user, Light::Through<project_user>"));

    // Both named types are forward-declared rather than included.
    CHECK(header.contains("struct user;"));
    CHECK(header.contains("struct project_user;"));
}

TEST_CASE("CxxModelPrinter: notes that a one-to-one relation is emitted as a collection", "[CxxModelPrinter][relations]")
{
    // Lightweight has no HasOne type. Rather than pretend the relation is scalar, the generated header
    // says what it is and why - the same approach as the DECIMAL precision note.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "user", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "profile",
          .columns = { IdColumn(), ForeignKeyColumn("user_id", /*isUnique=*/true) },
          .foreignKeys = { ForeignKey("profile", "user_id", "user") },
          .primaryKeys = { "id" } },
    };

    auto printer = CxxModelPrinter { CxxModelPrinter::Config {} };
    printer.ResolveOrderAndPrintTable(tables);

    auto const header = printer.HeaderFileForTheTable("Models", "user");
    INFO("user.hpp:\n" << header);

    CHECK(header.contains("Light::HasMany<profile>"));
    CHECK(header.contains("uniquely indexed"));
    CHECK(header.contains("no HasOne type"));
}

TEST_CASE("CxxModelPrinter: relation members do not collide with column members", "[CxxModelPrinter][relations]")
{
    // A parent with a column already named like the child table: the relation member has to be
    // uniqued against it, or the generated struct declares the same name twice.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "",
          .name = "author",
          .columns = { IdColumn(), { .name = "book", .type = Varchar { 20 } } }, // collides with the relation
          .primaryKeys = { "id" } },
        { .schema = "",
          .name = "book",
          .columns = { IdColumn(), ForeignKeyColumn("author_id") },
          .foreignKeys = { ForeignKey("book", "author_id", "author") },
          .primaryKeys = { "id" } },
    };

    auto printer = CxxModelPrinter { CxxModelPrinter::Config {} };
    printer.ResolveOrderAndPrintTable(tables);

    auto const header = printer.HeaderFileForTheTable("Models", "author");
    INFO("author.hpp:\n" << header);

    // Both members exist, under different names.
    CHECK(header.contains("Light::HasMany<book>"));

    // Count the declarations of the colliding identifier: the column takes `book`, so the relation
    // must have been renamed.
    auto occurrences = size_t { 0 };
    for (auto offset = header.find("> book;"); offset != std::string::npos; offset = header.find("> book;", offset + 1))
        ++occurrences;
    CHECK(occurrences <= 1);
}

TEST_CASE("CxxModelPrinter: relation members do not collide with the referenced struct's own name",
          "[CxxModelPrinter][relations]")
{
    // With no column named after the child table, the relation member's default name (the child
    // table's own name) is identical to the forward-declared struct it names as its type - `struct
    // book;` and a member `book` in the very same class. Legal C++ (a member may shadow an outer type),
    // but the member declaration then "changes the meaning" of `book` for the rest of the class body,
    // which GCC (-Wchanges-meaning) rejects under -Werror, and which reads as though the member and its
    // own type were the same thing.
    auto const tables = std::vector<Lightweight::SqlSchema::Table> {
        { .schema = "", .name = "author", .columns = { IdColumn() }, .primaryKeys = { "id" } },
        { .schema = "",
          .name = "book",
          .columns = { IdColumn(), ForeignKeyColumn("author_id") },
          .foreignKeys = { ForeignKey("book", "author_id", "author") },
          .primaryKeys = { "id" } },
    };

    auto printer = CxxModelPrinter { CxxModelPrinter::Config {} };
    printer.ResolveOrderAndPrintTable(tables);

    auto const header = printer.HeaderFileForTheTable("Models", "author");
    INFO("author.hpp:\n" << header);

    // The type is still forward-declared and still named by the relation...
    CHECK(header.contains("struct book;"));
    CHECK(header.contains("Light::HasMany<book>"));
    // ...but the member itself must not be named exactly `book`, or it would shadow that very type.
    CHECK_FALSE(header.contains("> book;"));
}
