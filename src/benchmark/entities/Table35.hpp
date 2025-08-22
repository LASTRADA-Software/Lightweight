// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table17.hpp"
#include "Table18.hpp"
#include "Table23.hpp"
#include "Table26.hpp"
#include "Table27.hpp"
#include "Table29.hpp"
#include "Table32.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table4.hpp"
#include "Table6.hpp"
#include "Table7.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table35 final
{
    static constexpr std::string_view TableName = "table_35";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<double, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<double, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<double, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_34" }> col34;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_35" }> col35;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table26::id, Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
    Light::BelongsTo<&Table13::id, Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<&Table33::id, Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
};

