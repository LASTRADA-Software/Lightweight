// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/SqlBackup.hpp>
#include <Lightweight/SqlColumnTypeDefinitions.hpp>
#include <Lightweight/SqlQuery/MigrationPlan.hpp>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <string_view>

using Lightweight::SqlBackup::ParseSchema;
using Lightweight::SqlBackup::TableInfo;
namespace TypeDefs = Lightweight::SqlColumnTypeDefinitions;
using Lightweight::SqlPrimaryKeyType;

namespace
{

// Build a minimal metadata JSON for a single table named "t". Lets each test vary
// only the relevant column-type fragment; the surrounding shape stays fixed.
//
// IMPORTANT: keep ParseSchema's return value alive (`auto const schema = ParseSchema(meta);`)
// for the whole test body. The map owns the SqlColumnDeclaration values that hold the
// column-type variant — a reference into the map (or its sub-structures) dangles the moment
// the temporary returned by ParseSchema goes out of scope.
std::string MakeOneColumnMetadata(std::string_view columnJson)
{
    return std::string {
        R"({"format_version":"1.0","creation_time":"2026-01-01T00:00:00Z","original_connection_string":"","schema_name":"","schema":[{"name":"t","rows":0,"columns":[)"
    } + std::string { columnJson }
           + "]}]}";
}

} // namespace

// ================================================================================================
// Each scalar column type — one section per JSON `type` string the parser recognizes
// ================================================================================================

TEST_CASE("ParseSchema: integer-family column types", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"a","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"integer"},
           {"name":"b","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"bigint"},
           {"name":"c","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"smallint"},
           {"name":"d","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"tinyint"})");

    auto const schema = ParseSchema(meta);
    auto const& cols = schema.at("t").columns;
    REQUIRE(cols.size() == 4);
    CHECK(std::holds_alternative<TypeDefs::Integer>(cols[0].type));
    CHECK(std::holds_alternative<TypeDefs::Bigint>(cols[1].type));
    CHECK(std::holds_alternative<TypeDefs::Smallint>(cols[2].type));
    CHECK(std::holds_alternative<TypeDefs::Tinyint>(cols[3].type));
}

TEST_CASE("ParseSchema: real preserves the precision field", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"a","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"real","precision":24})");

    auto const schema = ParseSchema(meta);
    auto const& cols = schema.at("t").columns;
    REQUIRE(cols.size() == 1);
    auto const& real = std::get<TypeDefs::Real>(cols[0].type);
    CHECK(real.precision == 24);
}

TEST_CASE("ParseSchema: real defaults precision to 53 (double) when absent", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"a","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"real"})");

    auto const schema = ParseSchema(meta);
    auto const& real = std::get<TypeDefs::Real>(schema.at("t").columns.at(0).type);
    CHECK(real.precision == 53);
}

TEST_CASE("ParseSchema: text / varchar / nvarchar / char / nchar carry the size", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"a","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"text"},
           {"name":"b","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"varchar","size":120},
           {"name":"c","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"nvarchar","size":250},
           {"name":"d","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"char","size":3},
           {"name":"e","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"nchar","size":5})");

    auto const schema = ParseSchema(meta);
    auto const& cols = schema.at("t").columns;
    REQUIRE(cols.size() == 5);
    CHECK(std::holds_alternative<TypeDefs::Text>(cols[0].type));
    CHECK(std::get<TypeDefs::Varchar>(cols[1].type).size == 120);
    CHECK(std::get<TypeDefs::NVarchar>(cols[2].type).size == 250);
    CHECK(std::get<TypeDefs::Char>(cols[3].type).size == 3);
    CHECK(std::get<TypeDefs::NChar>(cols[4].type).size == 5);
}

TEST_CASE("ParseSchema: binary and varbinary substitute 65535 when size is zero", "[SqlBackup][ParseSchema]")
{
    // The fallback exists because some sources record binary columns as having size=0
    // (meaning "max"); the parser substitutes 65535 to keep downstream code working.
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"a","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"binary","size":0},
           {"name":"b","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"varbinary","size":0},
           {"name":"c","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"binary","size":16},
           {"name":"d","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"varbinary","size":32})");

    auto const schema = ParseSchema(meta);
    auto const& cols = schema.at("t").columns;
    REQUIRE(cols.size() == 4);
    CHECK(std::get<TypeDefs::Binary>(cols[0].type).size == 65535);
    CHECK(std::get<TypeDefs::VarBinary>(cols[1].type).size == 65535);
    CHECK(std::get<TypeDefs::Binary>(cols[2].type).size == 16);
    CHECK(std::get<TypeDefs::VarBinary>(cols[3].type).size == 32);
}

TEST_CASE("ParseSchema: isBinaryColumn parallel vector marks every binary alternative", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"a","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"integer"},
           {"name":"b","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"binary","size":8},
           {"name":"c","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"varbinary","size":16},
           {"name":"d","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"text"})");

    auto const schema = ParseSchema(meta);
    auto const& info = schema.at("t");
    REQUIRE(info.isBinaryColumn.size() == 4);
    CHECK_FALSE(info.isBinaryColumn[0]);
    CHECK(info.isBinaryColumn[1]);
    CHECK(info.isBinaryColumn[2]);
    CHECK_FALSE(info.isBinaryColumn[3]);
}

TEST_CASE("ParseSchema: date / time / datetime / timestamp", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"a","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"date"},
           {"name":"b","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"datetime"},
           {"name":"c","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"time"},
           {"name":"d","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"timestamp"})");

    auto const schema = ParseSchema(meta);
    auto const& cols = schema.at("t").columns;
    REQUIRE(cols.size() == 4);
    CHECK(std::holds_alternative<TypeDefs::Date>(cols[0].type));
    CHECK(std::holds_alternative<TypeDefs::DateTime>(cols[1].type));
    CHECK(std::holds_alternative<TypeDefs::Time>(cols[2].type));
    CHECK(std::holds_alternative<TypeDefs::Timestamp>(cols[3].type));
}

TEST_CASE("ParseSchema: decimal carries precision and scale", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"a","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"decimal","precision":12,"scale":4})");

    auto const schema = ParseSchema(meta);
    auto const& dec = std::get<TypeDefs::Decimal>(schema.at("t").columns.at(0).type);
    CHECK(dec.precision == 12);
    CHECK(dec.scale == 4);
}

TEST_CASE("ParseSchema: decimal defaults to 18,2 when precision/scale absent", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"a","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"decimal"})");

    auto const schema = ParseSchema(meta);
    auto const& dec = std::get<TypeDefs::Decimal>(schema.at("t").columns.at(0).type);
    CHECK(dec.precision == 18);
    CHECK(dec.scale == 2);
}

TEST_CASE("ParseSchema: guid and bool", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"a","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"guid"},
           {"name":"b","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"bool"})");

    auto const schema = ParseSchema(meta);
    auto const& cols = schema.at("t").columns;
    REQUIRE(cols.size() == 2);
    CHECK(std::holds_alternative<TypeDefs::Guid>(cols[0].type));
    CHECK(std::holds_alternative<TypeDefs::Bool>(cols[1].type));
}

// ================================================================================================
// Flag fields and defaults
// ================================================================================================

TEST_CASE("ParseSchema: primary-key + auto-increment maps to AUTO_INCREMENT", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"id","is_primary_key":true,"is_auto_increment":true,"is_nullable":false,"is_unique":true,"type":"integer"})");

    auto const schema = ParseSchema(meta);
    auto const& col = schema.at("t").columns.at(0);
    CHECK(col.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT);
}

TEST_CASE("ParseSchema: primary-key without auto-increment maps to MANUAL", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"id","is_primary_key":true,"is_auto_increment":false,"is_nullable":false,"is_unique":true,"type":"integer"})");

    auto const schema = ParseSchema(meta);
    auto const& col = schema.at("t").columns.at(0);
    CHECK(col.primaryKey == SqlPrimaryKeyType::MANUAL);
}

TEST_CASE("ParseSchema: non-pk maps to NONE; required/unique flags flow through", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"email","is_primary_key":false,"is_auto_increment":false,"is_nullable":false,"is_unique":true,"default_value":"foo@bar","type":"varchar","size":50})");

    auto const schema = ParseSchema(meta);
    auto const& col = schema.at("t").columns.at(0);
    CHECK(col.primaryKey == SqlPrimaryKeyType::NONE);
    CHECK(col.required); // is_nullable == false → required == true
    CHECK(col.unique);
    CHECK(col.defaultValue == "foo@bar");
}

TEST_CASE("ParseSchema: rowCount is captured from the rows field", "[SqlBackup][ParseSchema]")
{
    constexpr std::string_view meta =
        R"({"format_version":"1.0","creation_time":"x","original_connection_string":"","schema_name":"","schema":[
            {"name":"users","rows":42,"columns":[{"name":"id","is_primary_key":true,"is_auto_increment":true,"is_nullable":false,"is_unique":true,"type":"integer"}]}
        ]})";
    auto const schema = ParseSchema(meta);
    CHECK(schema.at("users").rowCount == 42);
}

TEST_CASE("ParseSchema: 'fields' is a comma-separated double-quoted column list", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"id","is_primary_key":true,"is_auto_increment":true,"is_nullable":false,"is_unique":true,"type":"integer"},
           {"name":"first_name","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"text"})");
    auto const schema = ParseSchema(meta);
    CHECK(schema.at("t").fields == R"("id","first_name")");
}

// ================================================================================================
// Unknown type → warning callback + TEXT fallback (extends Tests.cpp's single existing case)
// ================================================================================================

TEST_CASE("ParseSchema: unknown column type falls back to TEXT and does not throw", "[SqlBackup][ParseSchema]")
{
    auto const meta = MakeOneColumnMetadata(
        R"({"name":"weird","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"never_heard_of_it"})");
    auto const schema = ParseSchema(meta, /*progress=*/nullptr);
    auto const& col = schema.at("t").columns.at(0);
    CHECK(std::holds_alternative<TypeDefs::Text>(col.type));
}

// ================================================================================================
// Foreign keys
// ================================================================================================

TEST_CASE("ParseSchema: foreign_keys block produces matching ForeignKeyConstraint entries", "[SqlBackup][ParseSchema]")
{
    constexpr std::string_view meta =
        R"({"format_version":"1.0","creation_time":"x","original_connection_string":"","schema_name":"","schema":[
            {"name":"posts","rows":0,"columns":[
                {"name":"id","is_primary_key":true,"is_auto_increment":true,"is_nullable":false,"is_unique":true,"type":"integer"},
                {"name":"user_id","is_primary_key":false,"is_auto_increment":false,"is_nullable":false,"is_unique":false,"type":"integer"}
            ],
             "foreign_keys":[
                {"columns":["user_id"],"referenced_table":"users","referenced_columns":["id"]}
             ]}
        ]})";
    auto const schema = ParseSchema(meta);
    auto const& info = schema.at("posts");
    REQUIRE(info.foreignKeys.size() == 1);
    auto const& fk = info.foreignKeys.front();
    CHECK(fk.foreignKey.table.table == "posts");
    REQUIRE(fk.foreignKey.columns.size() == 1);
    CHECK(fk.foreignKey.columns.front() == "user_id");
    CHECK(fk.primaryKey.table.table == "users");
    REQUIRE(fk.primaryKey.columns.size() == 1);
    CHECK(fk.primaryKey.columns.front() == "id");
}

// ================================================================================================
// Indexes
// ================================================================================================

TEST_CASE("ParseSchema: indexes block produces matching IndexDefinition entries", "[SqlBackup][ParseSchema]")
{
    constexpr std::string_view meta =
        R"({"format_version":"1.0","creation_time":"x","original_connection_string":"","schema_name":"","schema":[
            {"name":"users","rows":0,"columns":[
                {"name":"id","is_primary_key":true,"is_auto_increment":true,"is_nullable":false,"is_unique":true,"type":"integer"},
                {"name":"email","is_primary_key":false,"is_auto_increment":false,"is_nullable":true,"is_unique":false,"type":"text"}
            ],
             "indexes":[
                {"name":"idx_users_email","columns":["email"],"is_unique":true}
             ]}
        ]})";
    auto const schema = ParseSchema(meta);
    auto const& info = schema.at("users");
    REQUIRE(info.indexes.size() == 1);
    auto const& idx = info.indexes.front();
    CHECK(idx.name == "idx_users_email");
    REQUIRE(idx.columns.size() == 1);
    CHECK(idx.columns.front() == "email");
    CHECK(idx.isUnique);
}

// ================================================================================================
// primary_keys block — sets primaryKeyIndex and upgrades NONE → MANUAL but preserves AUTO_INCREMENT
// ================================================================================================

TEST_CASE("ParseSchema: primary_keys list assigns 1-based positions in declaration order", "[SqlBackup][ParseSchema]")
{
    // Composite PK on (tenant_id, user_id). Neither is auto-incrementing, so both must
    // become MANUAL and pick up positions 1 and 2.
    constexpr std::string_view meta =
        R"({"format_version":"1.0","creation_time":"x","original_connection_string":"","schema_name":"","schema":[
            {"name":"memberships","rows":0,"columns":[
                {"name":"tenant_id","is_primary_key":false,"is_auto_increment":false,"is_nullable":false,"is_unique":false,"type":"integer"},
                {"name":"user_id","is_primary_key":false,"is_auto_increment":false,"is_nullable":false,"is_unique":false,"type":"integer"}
            ],
             "primary_keys":["tenant_id","user_id"]}
        ]})";
    auto const schema = ParseSchema(meta);
    auto const& cols = schema.at("memberships").columns;
    REQUIRE(cols.size() == 2);
    CHECK(cols[0].primaryKey == SqlPrimaryKeyType::MANUAL);
    CHECK(cols[0].primaryKeyIndex == 1);
    CHECK(cols[1].primaryKey == SqlPrimaryKeyType::MANUAL);
    CHECK(cols[1].primaryKeyIndex == 2);
}

TEST_CASE("ParseSchema: primary_keys block does not downgrade AUTO_INCREMENT to MANUAL", "[SqlBackup][ParseSchema]")
{
    // The id column is already AUTO_INCREMENT via is_primary_key+is_auto_increment.
    // The primary_keys list also names it — the parser must preserve AUTO_INCREMENT.
    constexpr std::string_view meta =
        R"({"format_version":"1.0","creation_time":"x","original_connection_string":"","schema_name":"","schema":[
            {"name":"users","rows":0,"columns":[
                {"name":"id","is_primary_key":true,"is_auto_increment":true,"is_nullable":false,"is_unique":true,"type":"integer"}
            ],
             "primary_keys":["id"]}
        ]})";
    auto const schema = ParseSchema(meta);
    auto const& col = schema.at("users").columns.at(0);
    CHECK(col.primaryKey == SqlPrimaryKeyType::AUTO_INCREMENT);
    CHECK(col.primaryKeyIndex == 1);
}
