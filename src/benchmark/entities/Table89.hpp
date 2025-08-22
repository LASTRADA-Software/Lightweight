// File is automatically generated using ddl2cpp.
#pragma once

#include "Table15.hpp"
#include "Table18.hpp"
#include "Table24.hpp"
#include "Table30.hpp"
#include "Table44.hpp"
#include "Table48.hpp"
#include "Table50.hpp"
#include "Table56.hpp"
#include "Table58.hpp"
#include "Table66.hpp"
#include "Table71.hpp"
#include "Table82.hpp"
#include "Table85.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table89 final
{
    static constexpr std::string_view TableName = "table_89";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<int32_t, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<int32_t, Light::SqlRealName { "col_9" }> col9;
    Light::Field<int32_t, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<bool, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<int32_t, Light::SqlRealName { "col_31" }> col31;
    Light::Field<double, Light::SqlRealName { "col_32" }> col32;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<bool, Light::SqlRealName { "col_39" }> col39;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<bool, Light::SqlRealName { "col_41" }> col41;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_42" }> col42;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_43" }> col43;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_44" }> col44;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_45" }> col45;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_46" }> col46;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }, Light::SqlNullable::Null> fk44;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }> fk71;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<&Table58::id, Light::SqlRealName { "fk_58" }> fk58;
    Light::BelongsTo<&Table82::id, Light::SqlRealName { "fk_82" }, Light::SqlNullable::Null> fk82;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }, Light::SqlNullable::Null> fk56;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table85::id, Light::SqlRealName { "fk_85" }, Light::SqlNullable::Null> fk85;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<&Table66::id, Light::SqlRealName { "fk_66" }, Light::SqlNullable::Null> fk66;
};

