// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table13.hpp"
#include "Table15.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table21.hpp"
#include "Table24.hpp"
#include "Table25.hpp"
#include "Table29.hpp"
#include "Table3.hpp"
#include "Table31.hpp"
#include "Table32.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table35.hpp"
#include "Table37.hpp"
#include "Table42.hpp"
#include "Table44.hpp"
#include "Table45.hpp"
#include "Table48.hpp"
#include "Table49.hpp"
#include "Table5.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table51 final
{
    static constexpr std::string_view TableName = "table_51";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<bool, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<int32_t, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<bool, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }> fk37;
    Light::BelongsTo<&Table25::id, Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table13::id, Light::SqlRealName { "fk_13" }> fk13;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<&Table42::id, Light::SqlRealName { "fk_42" }> fk42;
    Light::BelongsTo<&Table33::id, Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }> fk34;
};

