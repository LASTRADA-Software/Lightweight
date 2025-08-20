// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table12.hpp"
#include "Table16.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table21.hpp"
#include "Table22.hpp"
#include "Table23.hpp"
#include "Table24.hpp"
#include "Table28.hpp"
#include "Table29.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table32.hpp"
#include "Table34.hpp"
#include "Table36.hpp"
#include "Table4.hpp"
#include "Table42.hpp"
#include "Table43.hpp"
#include "Table7.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table46 final
{
    static constexpr std::string_view TableName = "table_46";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<bool, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_4" }> col4;
    Light::Field<double, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<double, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<int32_t, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<bool, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_28" }> col28;
    Light::Field<double, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_34" }> col34;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }> fk24;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<&Table42::id, Light::SqlRealName { "fk_42" }> fk42;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }> fk23;
};

