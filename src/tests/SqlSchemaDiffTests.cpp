// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlSchemaDiff.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace Lightweight;
using namespace Lightweight::SqlSchema;

namespace
{

Column MakeColumn(std::string name,
                  std::string typeStr = "INTEGER",
                  bool nullable = true,
                  bool primaryKey = false)
{
    auto c = Column {};
    c.name = std::move(name);
    c.dialectDependantTypeString = std::move(typeStr);
    c.isNullable = nullable;
    c.isPrimaryKey = primaryKey;
    return c;
}

Table MakeTable(std::string schema, std::string name, std::vector<Column> columns)
{
    auto t = Table {};
    t.schema = std::move(schema);
    t.name = std::move(name);
    t.columns = std::move(columns);
    for (auto const& c: t.columns)
        if (c.isPrimaryKey)
            t.primaryKeys.push_back(c.name);
    return t;
}

[[nodiscard]] TableDiff const* FindTable(SchemaDiff const& d, std::string_view name)
{
    auto const it = std::ranges::find_if(d.tables, [&](TableDiff const& t) { return t.name == name; });
    return it == d.tables.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("DiffSchemas: identical schemas produce empty diff", "[SqlSchemaDiff]")
{
    auto const tables = TableList {
        MakeTable("dbo", "users", { MakeColumn("id", "INTEGER", false, true), MakeColumn("name", "VARCHAR(50)") }),
    };
    auto const diff = DiffSchemas(tables, tables);
    CHECK(diff.Empty());
}

TEST_CASE("DiffSchemas: missing table on each side", "[SqlSchemaDiff]")
{
    auto const a = TableList {
        MakeTable("dbo", "users", { MakeColumn("id", "INTEGER", false, true) }),
        MakeTable("dbo", "posts", { MakeColumn("id", "INTEGER", false, true) }),
    };
    auto const b = TableList {
        MakeTable("dbo", "users", { MakeColumn("id", "INTEGER", false, true) }),
        MakeTable("dbo", "comments", { MakeColumn("id", "INTEGER", false, true) }),
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
        MakeTable("dbo", "users", { MakeColumn("id", "INTEGER", false, true), MakeColumn("nickname", "VARCHAR(20)") }),
    };
    auto const b = TableList {
        MakeTable("dbo", "users", { MakeColumn("id", "INTEGER", false, true) }),
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
        MakeTable("dbo", "users", { MakeColumn("email", "VARCHAR(50)") }),
    };
    auto const b = TableList {
        MakeTable("dbo", "users", { MakeColumn("email", "VARCHAR(100)") }),
    };

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 1);
    auto const& col = diff.tables.front().columns.front();
    CHECK(col.kind == DiffKind::Changed);
    CHECK(col.changedFields == std::vector<std::string> { "type" });
}

TEST_CASE("DiffSchemas: nullable drift", "[SqlSchemaDiff]")
{
    auto const a = TableList {
        MakeTable("dbo", "users", { MakeColumn("name", "VARCHAR(50)", true) }),
    };
    auto const b = TableList {
        MakeTable("dbo", "users", { MakeColumn("name", "VARCHAR(50)", false) }),
    };

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 1);
    auto const& col = diff.tables.front().columns.front();
    CHECK(col.changedFields == std::vector<std::string> { "nullable" });
}

TEST_CASE("DiffSchemas: primary key drift", "[SqlSchemaDiff]")
{
    auto const a = TableList {
        MakeTable("dbo", "t", { MakeColumn("id", "INTEGER", false, true) }),
    };
    auto const b = TableList {
        MakeTable("dbo", "t", { MakeColumn("uuid", "VARCHAR(36)", false, true) }),
    };

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 1);
    CHECK_FALSE(diff.tables.front().primaryKeyDiffs.empty());
}

TEST_CASE("DiffSchemas: index drift", "[SqlSchemaDiff]")
{
    auto a = TableList {
        MakeTable("dbo", "users", { MakeColumn("id", "INTEGER", false, true) }),
    };
    a.front().indexes.push_back(IndexDefinition { .name = "idx_users_id", .columns = { "id" }, .isUnique = false });
    auto b = a;
    b.front().indexes.front().isUnique = true; // diverge: unique-ness flips

    auto const diff = DiffSchemas(a, b);
    REQUIRE(diff.tables.size() == 1);
    CHECK_FALSE(diff.tables.front().indexDiffs.empty());
}
