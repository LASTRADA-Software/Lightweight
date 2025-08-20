// File is automatically generated using ddl2cpp.
#pragma once

#include "Table13.hpp"
#include "Table16.hpp"
#include "Table24.hpp"
#include "Table29.hpp"
#include "Table40.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table43 final
{
    static constexpr std::string_view TableName = "table_43";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<bool, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<double, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_26" }> col26;
    Light::BelongsTo<Member(Table40::id), Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<Member(Table24::id), Light::SqlRealName { "fk_24" }> fk24;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }> fk16;
};

