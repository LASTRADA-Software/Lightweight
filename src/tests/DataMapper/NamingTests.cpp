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

    static_assert(QuotedFieldNameOf<Member(NamingTest1::normal)> == R"("NamingTest1"."normal")");
    static_assert(QuotedFieldNameOf<Member(NamingTest1::name)> == R"("NamingTest1"."c1")");
    static_assert(QuotedFieldNameOf<Member(NamingTest2::pk1)> == R"("NamingTest2_aliased"."First_PK")");
    static_assert(QuotedFieldNameOf<Member(NamingTest2::pk2)> == R"("NamingTest2_aliased"."Second_PK")");
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
