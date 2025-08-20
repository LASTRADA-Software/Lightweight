// File is automatically generated using ddl2cpp.
#pragma once

#include "Table11.hpp"
#include "Table12.hpp"
#include "Table19.hpp"
#include "Table21.hpp"
#include "Table26.hpp"
#include "Table3.hpp"
#include "Table33.hpp"
#include "Table36.hpp"
#include "Table39.hpp"
#include "Table6.hpp"
#include "Table8.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table44 final
{
    static constexpr std::string_view TableName = "table_44";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_0" }> col0;
    Light::Field<double, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<double, Light::SqlRealName { "col_5" }> col5;
    Light::Field<int32_t, Light::SqlRealName { "col_6" }> col6;
    Light::Field<int32_t, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<double, Light::SqlRealName { "col_10" }> col10;
    Light::Field<bool, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<bool, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<bool, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_19" }> col19;
    Light::BelongsTo<Member(Table19::id), Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }> fk11;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }> fk33;
    Light::BelongsTo<Member(Table26::id), Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }, Light::SqlNullable::Null> fk39;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<Member(Table21::id), Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<Member(Table9::id), Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<Member(Table6::id), Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
};

