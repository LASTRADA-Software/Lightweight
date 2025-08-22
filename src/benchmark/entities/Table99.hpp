// File is automatically generated using ddl2cpp.
#pragma once

#include "Table27.hpp"
#include "Table36.hpp"
#include "Table37.hpp"
#include "Table39.hpp"
#include "Table48.hpp"
#include "Table51.hpp"
#include "Table54.hpp"
#include "Table57.hpp"
#include "Table59.hpp"
#include "Table62.hpp"
#include "Table64.hpp"
#include "Table65.hpp"
#include "Table66.hpp"
#include "Table7.hpp"
#include "Table71.hpp"
#include "Table74.hpp"
#include "Table75.hpp"
#include "Table76.hpp"
#include "Table79.hpp"
#include "Table80.hpp"
#include "Table82.hpp"
#include "Table83.hpp"
#include "Table84.hpp"
#include "Table85.hpp"
#include "Table86.hpp"
#include "Table90.hpp"
#include "Table93.hpp"
#include "Table94.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table99 final
{
    static constexpr std::string_view TableName = "table_99";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<double, Light::SqlRealName { "col_10" }> col10;
    Light::Field<int32_t, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_19" }> col19;
    Light::Field<double, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<double, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<double, Light::SqlRealName { "col_34" }> col34;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_35" }> col35;
    Light::Field<bool, Light::SqlRealName { "col_36" }> col36;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_42" }> col42;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_43" }> col43;
    Light::Field<int32_t, Light::SqlRealName { "col_44" }> col44;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_45" }> col45;
    Light::Field<int32_t, Light::SqlRealName { "col_46" }> col46;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<&Table79::id, Light::SqlRealName { "fk_79" }, Light::SqlNullable::Null> fk79;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<&Table59::id, Light::SqlRealName { "fk_59" }, Light::SqlNullable::Null> fk59;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }> fk51;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table85::id, Light::SqlRealName { "fk_85" }> fk85;
    Light::BelongsTo<&Table86::id, Light::SqlRealName { "fk_86" }> fk86;
    Light::BelongsTo<&Table57::id, Light::SqlRealName { "fk_57" }> fk57;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }> fk62;
    Light::BelongsTo<&Table74::id, Light::SqlRealName { "fk_74" }, Light::SqlNullable::Null> fk74;
    Light::BelongsTo<&Table94::id, Light::SqlRealName { "fk_94" }, Light::SqlNullable::Null> fk94;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<&Table66::id, Light::SqlRealName { "fk_66" }> fk66;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }, Light::SqlNullable::Null> fk39;
    Light::BelongsTo<&Table64::id, Light::SqlRealName { "fk_64" }, Light::SqlNullable::Null> fk64;
    Light::BelongsTo<&Table90::id, Light::SqlRealName { "fk_90" }, Light::SqlNullable::Null> fk90;
    Light::BelongsTo<&Table93::id, Light::SqlRealName { "fk_93" }> fk93;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<&Table83::id, Light::SqlRealName { "fk_83" }, Light::SqlNullable::Null> fk83;
    Light::BelongsTo<&Table80::id, Light::SqlRealName { "fk_80" }> fk80;
    Light::BelongsTo<&Table84::id, Light::SqlRealName { "fk_84" }> fk84;
    Light::BelongsTo<&Table76::id, Light::SqlRealName { "fk_76" }> fk76;
    Light::BelongsTo<&Table65::id, Light::SqlRealName { "fk_65" }> fk65;
    Light::BelongsTo<&Table75::id, Light::SqlRealName { "fk_75" }, Light::SqlNullable::Null> fk75;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }> fk37;
    Light::BelongsTo<&Table54::id, Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<&Table82::id, Light::SqlRealName { "fk_82" }, Light::SqlNullable::Null> fk82;
};

