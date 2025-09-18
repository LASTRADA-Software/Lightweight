// File is automatically generated using ddl2cpp.
#pragma once

#include "Table22.hpp"
#include "Table23.hpp"
#include "Table32.hpp"
#include "Table36.hpp"
#include "Table37.hpp"
#include "Table50.hpp"
#include "Table64.hpp"
#include "Table75.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table77 final
{
    static constexpr std::string_view TableName = "table_77";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<double, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<int32_t, Light::SqlRealName { "col_9" }> col9;
    Light::Field<bool, Light::SqlRealName { "col_10" }> col10;
    Light::Field<double, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<double, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<bool, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<double, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<int32_t, Light::SqlRealName { "col_32" }> col32;
    Light::Field<double, Light::SqlRealName { "col_33" }> col33;
    Light::Field<double, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_37" }> col37;
    Light::BelongsTo<Member(Table64::id), Light::SqlRealName { "fk_64" }> fk64;
    Light::BelongsTo<Member(Table32::id), Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<Member(Table75::id), Light::SqlRealName { "fk_75" }, Light::SqlNullable::Null> fk75;
    Light::BelongsTo<Member(Table37::id), Light::SqlRealName { "fk_37" }> fk37;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<Member(Table23::id), Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<Member(Table50::id), Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }> fk36;
};

