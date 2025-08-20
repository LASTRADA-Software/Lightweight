// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table10.hpp"
#include "Table11.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table2.hpp"
#include "Table21.hpp"
#include "Table23.hpp"
#include "Table25.hpp"
#include "Table26.hpp"
#include "Table27.hpp"
#include "Table28.hpp"
#include "Table3.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table4.hpp"
#include "Table5.hpp"
#include "Table6.hpp"
#include "Table7.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table32 final
{
    static constexpr std::string_view TableName = "table_32";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<int32_t, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<int32_t, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<int32_t, Light::SqlRealName { "col_17" }> col17;
    Light::Field<bool, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_33" }> col33;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<&Table13::id, Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
    Light::BelongsTo<&Table25::id, Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
    Light::BelongsTo<&Table26::id, Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
};

