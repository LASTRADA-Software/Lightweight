// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlSchemaDiff.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace Lightweight;
using namespace Lightweight::SqlSchema;

namespace
{

[[nodiscard]] TableDiff const* FindTable(SchemaDiff const& d, std::string_view name)
{
    auto const it = std::ranges::find_if(d.tables, [&](TableDiff const& t) { return t.name == name; });
    return it == d.tables.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("DiffSchemas: identical schemas produce empty diff", "[SqlSchemaDiff]")
{
    auto const tables = TableList {
        Table {
            .schema = "dbo",
            .name = "users",
            .columns = { Column { .name = "id",
                                  .type = SqlColumnTypeDefinitions::Integer {},
                                  .isNullable = false,
                                  .isPrimaryKey = true },
                         Column { .name = "name", .type = SqlColumnTypeDefinitions::Varchar { 50 }, .size = 50 } },
            .primaryKeys = { "id" },
        },
    };
    auto const diff = DiffSchemas(tables, tables);
    CHECK(diff.Empty());
}

TEST_CASE("DiffSchemas: missing table on each side", "[SqlSchemaDiff]")
{
    auto const idColumn = Column {
        .name = "id",
        .type = SqlColumnTypeDefinitions::Integer {},
        .isNullable = false,
        .isPrimaryKey = true,
    };
    auto const a = TableList {
        Table { .schema = "dbo", .name = "users", .columns = { idColumn }, .primaryKeys = { "id" } },
        Table { .schema = "dbo", .name = "posts", .columns = { idColumn }, .primaryKeys = { "id" } },
    };
    auto const b = TableList {
        Table { .schema = "dbo", .name = "users", .columns = { idColumn }, .primaryKeys = { "id" } },
        Table { .schema = "dbo", .name = "comments", .columns = { idColumn }, .primaryKeys = { "id" } },
    };

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 2);

    auto const* posts = FindTable(diff, "posts");
    REQUIRE(posts);
    CHECK(posts->kind == DiffKind::OnlyInA);

    auto const* comments = FindTable(diff, "comments");
    REQUIRE(comments);
    CHECK(comments->kind == DiffKind::OnlyInB);
}

TEST_CASE("DiffSchemas: missing column produces Changed table with column diff", "[SqlSchemaDiff]")
{
    auto const a = TableList {
        Table {
            .schema = "dbo",
            .name = "users",
            .columns = { Column { .name = "id",
                                  .type = SqlColumnTypeDefinitions::Integer {},
                                  .isNullable = false,
                                  .isPrimaryKey = true },
                         Column { .name = "nickname", .type = SqlColumnTypeDefinitions::Varchar { 20 }, .size = 20 } },
            .primaryKeys = { "id" },
        },
    };
    auto const b = TableList {
        Table {
            .schema = "dbo",
            .name = "users",
            .columns = { Column {
                .name = "id", .type = SqlColumnTypeDefinitions::Integer {}, .isNullable = false, .isPrimaryKey = true } },
            .primaryKeys = { "id" },
        },
    };

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 1);
    auto const& t = diff.tables.front();
    CHECK(t.kind == DiffKind::Changed);
    REQUIRE(t.columns.size() == 1);
    CHECK(t.columns.front().name == "nickname");
    CHECK(t.columns.front().kind == DiffKind::OnlyInA);
}

TEST_CASE("DiffSchemas: type drift on shared column", "[SqlSchemaDiff]")
{
    auto const a = TableList {
        Table {
            .schema = "dbo",
            .name = "users",
            .columns = { Column { .name = "email", .type = SqlColumnTypeDefinitions::Varchar { 50 }, .size = 50 } },
        },
    };
    auto const b = TableList {
        Table {
            .schema = "dbo",
            .name = "users",
            .columns = { Column { .name = "email", .type = SqlColumnTypeDefinitions::Varchar { 100 }, .size = 100 } },
        },
    };

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 1);
    auto const& col = diff.tables.front().columns.front();
    CHECK(col.kind == DiffKind::Changed);
    // The size of a Varchar lives inside the canonical type variant — it surfaces as a
    // `type` change, not a separate `size` change (the standalone driver-reported `size`
    // field is intentionally not compared, see DiffColumnFields).
    CHECK(col.changedFields == std::vector<std::string> { "type" });
}

TEST_CASE("DiffSchemas: nullable drift", "[SqlSchemaDiff]")
{
    auto const a = TableList {
        Table {
            .schema = "dbo",
            .name = "users",
            .columns = { Column {
                .name = "name", .type = SqlColumnTypeDefinitions::Varchar { 50 }, .isNullable = true, .size = 50 } },
        },
    };
    auto const b = TableList {
        Table {
            .schema = "dbo",
            .name = "users",
            .columns = { Column {
                .name = "name", .type = SqlColumnTypeDefinitions::Varchar { 50 }, .isNullable = false, .size = 50 } },
        },
    };

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 1);
    auto const& col = diff.tables.front().columns.front();
    CHECK(col.changedFields == std::vector<std::string> { "nullable" });
}

TEST_CASE("DiffSchemas: primary key drift", "[SqlSchemaDiff]")
{
    auto const a = TableList {
        Table {
            .schema = "dbo",
            .name = "t",
            .columns = { Column {
                .name = "id", .type = SqlColumnTypeDefinitions::Integer {}, .isNullable = false, .isPrimaryKey = true } },
            .primaryKeys = { "id" },
        },
    };
    auto const b = TableList {
        Table {
            .schema = "dbo",
            .name = "t",
            .columns = { Column { .name = "uuid",
                                  .type = SqlColumnTypeDefinitions::Varchar { 36 },
                                  .isNullable = false,
                                  .size = 36,
                                  .isPrimaryKey = true } },
            .primaryKeys = { "uuid" },
        },
    };

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 1);
    CHECK_FALSE(diff.tables.front().primaryKeyDiffs.empty());
}

TEST_CASE("DiffSchemas: index drift", "[SqlSchemaDiff]")
{
    auto a = TableList {
        Table {
            .schema = "dbo",
            .name = "users",
            .columns = { Column {
                .name = "id", .type = SqlColumnTypeDefinitions::Integer {}, .isNullable = false, .isPrimaryKey = true } },
            .primaryKeys = { "id" },
            .indexes = { IndexDefinition { .name = "idx_users_id", .columns = { "id" }, .isUnique = false } },
        },
    };
    auto b = a;
    b.front().indexes.front().isUnique = true; // diverge: unique-ness flips

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 1);
    CHECK_FALSE(diff.tables.front().indexDiffs.empty());
}

TEST_CASE("DiffSchemas: cross-engine schema labels still pair the same logical table", "[SqlSchemaDiff]")
{
    // Same logical table, but one side reports it under `public` (PostgreSQL) and the
    // other under `dbo` (MSSQL). The diff must recognise both as the same table.
    auto const idColumn = Column {
        .name = "id",
        .type = SqlColumnTypeDefinitions::Integer {},
        .isNullable = false,
        .isPrimaryKey = true,
    };
    auto const a = TableList {
        Table { .schema = "public", .name = "users", .columns = { idColumn }, .primaryKeys = { "id" } },
    };
    auto const b = TableList {
        Table { .schema = "dbo", .name = "users", .columns = { idColumn }, .primaryKeys = { "id" } },
    };

    auto const diff = DiffSchemas(a, b);
    CHECK(diff.Empty());
}

TEST_CASE("DiffSchemas: cross-engine schema labels surface on Changed entries", "[SqlSchemaDiff]")
{
    // Same logical table differs only on a column. Both sides must be paired despite their
    // different schema labels, and the result must carry both labels for the renderer.
    auto const idColumn = Column {
        .name = "id",
        .type = SqlColumnTypeDefinitions::Integer {},
        .isNullable = false,
        .isPrimaryKey = true,
    };
    auto const a = TableList {
        Table {
            .schema = "public",
            .name = "users",
            .columns = { idColumn, Column { .name = "name", .type = SqlColumnTypeDefinitions::Varchar { 50 }, .size = 50 } },
            .primaryKeys = { "id" },
        },
    };
    auto const b = TableList {
        Table {
            .schema = "dbo",
            .name = "users",
            .columns = { idColumn,
                         Column { .name = "name", .type = SqlColumnTypeDefinitions::Varchar { 100 }, .size = 100 } },
            .primaryKeys = { "id" },
        },
    };

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 1);
    auto const& t = diff.tables.front();
    CHECK(t.name == "users");
    CHECK(t.schemaA == "public");
    CHECK(t.schemaB == "dbo");
    CHECK(t.kind == DiffKind::Changed);
}

TEST_CASE("DiffSchemas: only-in-A keeps schemaA, only-in-B keeps schemaB", "[SqlSchemaDiff]")
{
    auto const idColumn = Column {
        .name = "id",
        .type = SqlColumnTypeDefinitions::Integer {},
        .isNullable = false,
        .isPrimaryKey = true,
    };
    auto const a = TableList {
        Table { .schema = "public", .name = "posts", .columns = { idColumn }, .primaryKeys = { "id" } },
    };
    auto const b = TableList {
        Table { .schema = "dbo", .name = "comments", .columns = { idColumn }, .primaryKeys = { "id" } },
    };

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 2);

    auto const* posts = FindTable(diff, "posts");
    REQUIRE(posts);
    CHECK(posts->kind == DiffKind::OnlyInA);
    CHECK(posts->schemaA == "public");
    CHECK(posts->schemaB.empty());

    auto const* comments = FindTable(diff, "comments");
    REQUIRE(comments);
    CHECK(comments->kind == DiffKind::OnlyInB);
    CHECK(comments->schemaA.empty());
    CHECK(comments->schemaB == "dbo");
}

TEST_CASE("DiffSchemas: NVarchar and Varchar are logically equivalent", "[SqlSchemaDiff]")
{
    // PostgreSQL formats migration `NVarchar(50)` as `VARCHAR(50)`; the driver therefore
    // returns it as `Varchar`. SQLite and MSSQL preserve `NVarchar`. The diff must treat
    // them as the same logical type so post-migration parity holds.
    auto const a = TableList {
        Table {
            .schema = "main",
            .name = "users",
            .columns = { Column { .name = "name", .type = SqlColumnTypeDefinitions::NVarchar { 50 }, .size = 50 } },
        },
    };
    auto const b = TableList {
        Table {
            .schema = "public",
            .name = "users",
            .columns = { Column { .name = "name", .type = SqlColumnTypeDefinitions::Varchar { 50 }, .size = 50 } },
        },
    };

    auto const diff = DiffSchemas(a, b);
    CHECK(diff.Empty());
}

TEST_CASE("DiffSchemas: NChar and Char are logically equivalent", "[SqlSchemaDiff]")
{
    auto const a = TableList {
        Table {
            .schema = "main",
            .name = "t",
            .columns = { Column { .name = "code", .type = SqlColumnTypeDefinitions::NChar { 30 }, .size = 30 } },
        },
    };
    auto const b = TableList {
        Table {
            .schema = "public",
            .name = "t",
            .columns = { Column { .name = "code", .type = SqlColumnTypeDefinitions::Char { 30 }, .size = 30 } },
        },
    };

    auto const diff = DiffSchemas(a, b);
    CHECK(diff.Empty());
}

TEST_CASE("DiffSchemas: Real precision drift is ignored", "[SqlSchemaDiff]")
{
    // PostgreSQL formats `Real{}` as `REAL` (float4 → reads back as Real{24}) while SQLite
    // round-trips as Real{53}. Migration intent is just "floating point"; precision drift
    // here is engine noise, not a logical difference.
    auto const a = TableList {
        Table {
            .schema = "main",
            .name = "t",
            .columns = { Column { .name = "v", .type = SqlColumnTypeDefinitions::Real { .precision = 53 } } },
        },
    };
    auto const b = TableList {
        Table {
            .schema = "public",
            .name = "t",
            .columns = { Column { .name = "v", .type = SqlColumnTypeDefinitions::Real { .precision = 24 } } },
        },
    };

    auto const diff = DiffSchemas(a, b);
    CHECK(diff.Empty());
}

TEST_CASE("DiffSchemas: unbounded text representations are equivalent", "[SqlSchemaDiff]")
{
    // SQLite reports `TEXT` columns as `Varchar` with a sentinel huge size; PostgreSQL
    // reports them with size 0 and the dialect string `text`. Both are intent = unbounded.
    auto const a = TableList {
        Table {
            .schema = "main",
            .name = "t",
            .columns = { Column { .name = "body", .type = SqlColumnTypeDefinitions::Varchar { 1'000'000'000 } } },
        },
    };
    auto const b = TableList {
        Table {
            .schema = "public",
            .name = "t",
            .columns = { Column { .name = "body", .type = SqlColumnTypeDefinitions::Text { 0 } } },
        },
    };

    auto const diff = DiffSchemas(a, b);
    CHECK(diff.Empty());
}

TEST_CASE("DiffSchemas: migration-internal `_migration_locks` is excluded", "[SqlSchemaDiff]")
{
    // SQLite creates a `_migration_locks` table for its lock implementation; PostgreSQL and
    // MSSQL use advisory locks. The diff must hide this engine-specific plumbing so a
    // genuinely-equivalent schema-migration run reads as equal across all engines.
    auto const a = TableList {
        Table {
            .schema = "main",
            .name = "_migration_locks",
            .columns = { Column { .name = "lock_name", .type = SqlColumnTypeDefinitions::Varchar { 255 } } },
        },
    };
    auto const b = TableList {};

    auto const diff = DiffSchemas(a, b);
    CHECK(diff.Empty());
}

TEST_CASE("DiffSchemas: foreign keys compare schema-agnostically", "[SqlSchemaDiff]")
{
    // Same FK constraint reported with different schema labels on each side. They must
    // compare as identical so the table is not flagged as Changed.
    auto a = TableList {
        Table {
            .schema = "public",
            .name = "orders",
            .columns = { Column { .name = "user_id", .type = SqlColumnTypeDefinitions::Integer {}, .isNullable = false } },
            .foreignKeys = { ForeignKeyConstraint {
                .foreignKey = { .table = { .catalog = {}, .schema = "public", .table = "orders" },
                                .columns = { "user_id" } },
                .primaryKey = { .table = { .catalog = {}, .schema = "public", .table = "users" }, .columns = { "id" } },
            } },
        },
    };
    auto b = TableList {
        Table {
            .schema = "dbo",
            .name = "orders",
            .columns = { Column { .name = "user_id", .type = SqlColumnTypeDefinitions::Integer {}, .isNullable = false } },
            .foreignKeys = { ForeignKeyConstraint {
                .foreignKey = { .table = { .catalog = {}, .schema = "dbo", .table = "orders" }, .columns = { "user_id" } },
                .primaryKey = { .table = { .catalog = {}, .schema = "dbo", .table = "users" }, .columns = { "id" } },
            } },
        },
    };

    auto const diff = DiffSchemas(a, b);
    CHECK(diff.Empty());
}
