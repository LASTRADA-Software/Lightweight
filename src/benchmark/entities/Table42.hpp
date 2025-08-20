// File is automatically generated using ddl2cpp.
#pragma once

#include "Table11.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table16.hpp"
#include "Table25.hpp"
#include "Table27.hpp"
#include "Table31.hpp"
#include "Table35.hpp"
#include "Table36.hpp"
#include "Table7.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table42 final
{
    static constexpr std::string_view TableName = "table_42";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<bool, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<int32_t, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_10" }> col10;
    Light::Field<double, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<double, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_22" }> col22;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_25" }> col25;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }> fk25;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }> fk13;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }> fk7;
};

