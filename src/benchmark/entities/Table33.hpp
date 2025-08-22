// File is automatically generated using ddl2cpp.
#pragma once

#include "Table12.hpp"
#include "Table23.hpp"
#include "Table25.hpp"
#include "Table27.hpp"
#include "Table3.hpp"
#include "Table31.hpp"
#include "Table7.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table33 final
{
    static constexpr std::string_view TableName = "table_33";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<int32_t, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<double, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<double, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_21" }> col21;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<&Table25::id, Light::SqlRealName { "fk_25" }> fk25;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
};

