// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table15.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table22.hpp"
#include "Table28.hpp"
#include "Table29.hpp"
#include "Table30.hpp"
#include "Table32.hpp"
#include "Table37.hpp"
#include "Table4.hpp"
#include "Table40.hpp"
#include "Table41.hpp"
#include "Table5.hpp"
#include "Table7.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table44 final
{
    static constexpr std::string_view TableName = "table_44";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<bool, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<int32_t, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<bool, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<int32_t, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_30" }> col30;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }> fk37;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
};

