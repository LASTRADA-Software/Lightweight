// File is automatically generated using ddl2cpp.
#pragma once

#include "Table10.hpp"
#include "Table11.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table22.hpp"
#include "Table24.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table39.hpp"
#include "Table40.hpp"
#include "Table45.hpp"
#include "Table51.hpp"
#include "Table53.hpp"
#include "Table55.hpp"
#include "Table56.hpp"
#include "Table62.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table69 final
{
    static constexpr std::string_view TableName = "table_69";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<int32_t, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<int32_t, Light::SqlRealName { "col_5" }> col5;
    Light::Field<int32_t, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<int32_t, Light::SqlRealName { "col_8" }> col8;
    Light::Field<double, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_16" }> col16;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<&Table53::id, Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }> fk10;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }> fk51;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }> fk11;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }> fk62;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
};

