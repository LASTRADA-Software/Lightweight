// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <reflection-cpp/reflection.hpp>

#include <catch2/catch_session.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string_view>

using namespace std::string_view_literals;
using namespace std::string_literals;
using namespace Lightweight;

struct NamingTest1
{
    Field<int> normal;
    Field<int, SqlRealName { "c1" }> name;
};

struct NamingTest2
{
    Field<int, PrimaryKey::AutoAssign, SqlRealName { "First_PK" }> pk1;
    Field<int, SqlRealName { "Second_PK" }, PrimaryKey::AutoAssign> pk2;

    static constexpr std::string_view TableName = "NamingTest2_aliased"sv;
};

TEST_CASE_METHOD(SqlTestFixture, "SQL entity naming", "[DataMapper]")
{
    static_assert(RecordTableName<NamingTest1> == "NamingTest1"sv);
    static_assert(RecordTableName<NamingTest2> == "NamingTest2_aliased"sv);

    static_assert(FieldNameAt<0, NamingTest1> == "normal"sv);
    static_assert(FieldNameAt<1, NamingTest1> == "c1"sv);
    static_assert(FieldNameAt<0, NamingTest2> == "First_PK"sv);
    static_assert(FieldNameAt<1, NamingTest2> == "Second_PK"sv);

    static_assert(FieldNameOf<Member(NamingTest1::normal)> == "normal"sv);
    static_assert(FieldNameOf<Member(NamingTest1::name)> == "c1"sv);
    static_assert(FieldNameOf<Member(NamingTest2::pk1)> == "First_PK"sv);
    static_assert(FieldNameOf<Member(NamingTest2::pk2)> == "Second_PK"sv);

    static_assert(FullyQualifiedNameOf<Member(NamingTest1::normal)> == R"("NamingTest1"."normal")");
    static_assert(FullyQualifiedNameOf<Member(NamingTest1::name)> == R"("NamingTest1"."c1")");
    static_assert(FullyQualifiedNameOf<Member(NamingTest2::pk1)> == R"("NamingTest2_aliased"."First_PK")");
    static_assert(FullyQualifiedNameOf<Member(NamingTest2::pk2)> == R"("NamingTest2_aliased"."Second_PK")");
}

namespace Models
{

struct NamingTest1
{
    Field<int> normal;
    Field<int, SqlRealName { "c1" }> name;
};

struct NamingTest2
{
    Field<int, PrimaryKey::AutoAssign, SqlRealName { "First_PK" }> pk1;
    Field<int, SqlRealName { "Second_PK" }, PrimaryKey::AutoAssign> pk2;

    static constexpr std::string_view TableName = "NamingTest2_aliased"sv;
};

struct Person
{
    Light::Field<int, Light::PrimaryKey::AutoAssign, Light::SqlRealName { "index" }> id;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "not_name" }> name;
    static constexpr std::string_view TableName = "Human"sv;
};

} // namespace Models

TEST_CASE_METHOD(SqlTestFixture, "SQL entity naming (namespace)", "[DataMapper]")
{
    CHECK(FieldNameAt<0, Models::NamingTest1> == "normal"sv);
    CHECK(FieldNameAt<1, Models::NamingTest1> == "c1"sv);
    CHECK(RecordTableName<Models::NamingTest1> == "NamingTest1"sv);

    CHECK(FieldNameAt<0, Models::NamingTest2> == "First_PK"sv);
    CHECK(FieldNameAt<1, Models::NamingTest2> == "Second_PK"sv);
    CHECK(RecordTableName<Models::NamingTest2> == "NamingTest2_aliased"sv);
}

TEST_CASE_METHOD(SqlTestFixture, "Check aliasing of the columns and table for crud operation", "[DataMapper]")
{
    auto dm = DataMapper();

    dm.CreateTable<Models::Person>();

    auto record1 = Models::Person {};
    record1.name = "42";

    CHECK(dm.Query<Models::Person>().Count() == 0);
    CHECK(record1.id.Value() == 0);
    dm.Create(record1);
    CHECK(dm.Query<Models::Person>().Count() == 1);
    CHECK(record1.id.Value() != 0);
    {
        auto queriedRecordResult = dm.QuerySingle<Models::Person>(record1.id);
        CHECK(queriedRecordResult.has_value());
        CHECK(queriedRecordResult.value().name.Value().ToStringView() // NOLINT(bugprone-unchecked-optional-access)
              == "42"sv);
    }

    record1.name = "43";
    dm.Update(record1);

    SqlStatement(dm.Connection())
        .ExecuteDirect(R"( INSERT INTO "Human" ("index", "not_name") VALUES (5, 'Direct Insert') )");
    CHECK(dm.Query<Models::Person>().Count() == 2);
    {
        auto queriedRecordResult = dm.QuerySingle<Models::Person>(record1.id);
        CHECK(queriedRecordResult.has_value());
        CHECK(queriedRecordResult.value().name.Value().ToStringView() // NOLINT(bugprone-unchecked-optional-access)
              == "43"sv);
    }
    dm.Delete(record1);
    CHECK(dm.Query<Models::Person>().Count() == 1);
    {
        auto queriedRecord = dm.QuerySingle<Models::Person>(record1.id);
        CHECK(!queriedRecord.has_value());
    }
}
