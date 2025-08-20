// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table10.hpp"
#include "Table13.hpp"
#include "Table29.hpp"
#include "Table34.hpp"
#include "Table35.hpp"
#include "Table46.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table58.hpp"
#include "Table59.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table70 final
{
    static constexpr std::string_view TableName = "table_70";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<int32_t, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<int32_t, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_16" }> col16;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }> fk13;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<Member(Table52::id), Light::SqlRealName { "fk_52" }> fk52;
    Light::BelongsTo<Member(Table59::id), Light::SqlRealName { "fk_59" }, Light::SqlNullable::Null> fk59;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<Member(Table58::id), Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<Member(Table46::id), Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<Member(Table0::id), Light::SqlRealName { "fk_0" }> fk0;
};

