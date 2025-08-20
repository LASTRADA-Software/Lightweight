// File is automatically generated using ddl2cpp.
#pragma once

#include "Table10.hpp"
#include "Table11.hpp"
#include "Table18.hpp"
#include "Table26.hpp"
#include "Table43.hpp"
#include "Table45.hpp"
#include "Table47.hpp"
#include "Table48.hpp"
#include "Table56.hpp"
#include "Table70.hpp"
#include "Table71.hpp"
#include "Table75.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table81 final
{
    static constexpr std::string_view TableName = "table_81";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<int32_t, Light::SqlRealName { "col_6" }> col6;
    Light::Field<double, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<double, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<bool, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_36" }> col36;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_40" }> col40;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_42" }> col42;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }> fk47;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table26::id, Light::SqlRealName { "fk_26" }, Light::SqlNullable::Null> fk26;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }> fk10;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }, Light::SqlNullable::Null> fk45;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }> fk71;
    Light::BelongsTo<&Table75::id, Light::SqlRealName { "fk_75" }, Light::SqlNullable::Null> fk75;
    Light::BelongsTo<&Table70::id, Light::SqlRealName { "fk_70" }, Light::SqlNullable::Null> fk70;
};

