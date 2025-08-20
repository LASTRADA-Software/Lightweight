// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table11.hpp"
#include "Table12.hpp"
#include "Table13.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table20.hpp"
#include "Table21.hpp"
#include "Table22.hpp"
#include "Table23.hpp"
#include "Table25.hpp"
#include "Table27.hpp"
#include "Table28.hpp"
#include "Table32.hpp"
#include "Table4.hpp"
#include "Table40.hpp"
#include "Table42.hpp"
#include "Table43.hpp"
#include "Table47.hpp"
#include "Table48.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table51.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table54.hpp"
#include "Table6.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table55 final
{
    static constexpr std::string_view TableName = "table_55";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<bool, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<bool, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<double, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<int32_t, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<double, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<double, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<double, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<int32_t, Light::SqlRealName { "col_22" }> col22;
    Light::Field<bool, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_25" }> col25;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table25::id, Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
    Light::BelongsTo<&Table13::id, Light::SqlRealName { "fk_13" }> fk13;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }> fk47;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }> fk43;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }> fk52;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<&Table42::id, Light::SqlRealName { "fk_42" }, Light::SqlNullable::Null> fk42;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }> fk19;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<&Table53::id, Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }> fk20;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }> fk40;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }> fk48;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
    Light::BelongsTo<&Table54::id, Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
};

