// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table12.hpp"
#include "Table19.hpp"
#include "Table3.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table58.hpp"
#include "Table59.hpp"
#include "Table63.hpp"
#include "Table66.hpp"
#include "Table70.hpp"
#include "Table71.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table75 final
{
    static constexpr std::string_view TableName = "table_75";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<double, Light::SqlRealName { "col_2" }> col2;
    Light::Field<bool, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<int32_t, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_32" }> col32;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_33" }> col33;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<bool, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_40" }> col40;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<&Table66::id, Light::SqlRealName { "fk_66" }> fk66;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }> fk19;
    Light::BelongsTo<&Table58::id, Light::SqlRealName { "fk_58" }> fk58;
    Light::BelongsTo<&Table70::id, Light::SqlRealName { "fk_70" }, Light::SqlNullable::Null> fk70;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }> fk50;
    Light::BelongsTo<&Table59::id, Light::SqlRealName { "fk_59" }> fk59;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table63::id, Light::SqlRealName { "fk_63" }, Light::SqlNullable::Null> fk63;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
};

