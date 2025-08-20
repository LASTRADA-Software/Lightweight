// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table22.hpp"
#include "Table25.hpp"
#include "Table3.hpp"
#include "Table30.hpp"
#include "Table36.hpp"
#include "Table46.hpp"
#include "Table52.hpp"
#include "Table55.hpp"
#include "Table62.hpp"
#include "Table68.hpp"
#include "Table74.hpp"
#include "Table78.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table90 final
{
    static constexpr std::string_view TableName = "table_90";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<double, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<double, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<bool, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<int32_t, Light::SqlRealName { "col_28" }> col28;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<&Table74::id, Light::SqlRealName { "fk_74" }> fk74;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }> fk52;
    Light::BelongsTo<&Table68::id, Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }> fk19;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }> fk20;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<&Table78::id, Light::SqlRealName { "fk_78" }> fk78;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<&Table25::id, Light::SqlRealName { "fk_25" }> fk25;
};

