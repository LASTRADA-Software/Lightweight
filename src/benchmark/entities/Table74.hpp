// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table10.hpp"
#include "Table11.hpp"
#include "Table16.hpp"
#include "Table21.hpp"
#include "Table22.hpp"
#include "Table23.hpp"
#include "Table24.hpp"
#include "Table32.hpp"
#include "Table38.hpp"
#include "Table42.hpp"
#include "Table44.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table55.hpp"
#include "Table56.hpp"
#include "Table59.hpp"
#include "Table6.hpp"
#include "Table70.hpp"
#include "Table71.hpp"
#include "Table8.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table74 final
{
    static constexpr std::string_view TableName = "table_74";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<double, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<int32_t, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<bool, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<bool, Light::SqlRealName { "col_22" }> col22;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }> fk24;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<&Table59::id, Light::SqlRealName { "fk_59" }> fk59;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<&Table70::id, Light::SqlRealName { "fk_70" }, Light::SqlNullable::Null> fk70;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table53::id, Light::SqlRealName { "fk_53" }> fk53;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<&Table42::id, Light::SqlRealName { "fk_42" }> fk42;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }, Light::SqlNullable::Null> fk44;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
};

