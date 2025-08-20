// File is automatically generated using ddl2cpp.
#pragma once

#include "Table10.hpp"
#include "Table12.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table17.hpp"
#include "Table18.hpp"
#include "Table23.hpp"
#include "Table27.hpp"
#include "Table28.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table36.hpp"
#include "Table38.hpp"
#include "Table39.hpp"
#include "Table40.hpp"
#include "Table42.hpp"
#include "Table43.hpp"
#include "Table45.hpp"
#include "Table47.hpp"
#include "Table5.hpp"
#include "Table51.hpp"
#include "Table54.hpp"
#include "Table58.hpp"
#include "Table59.hpp"
#include "Table6.hpp"
#include "Table7.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table60 final
{
    static constexpr std::string_view TableName = "table_60";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<bool, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<bool, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<bool, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<bool, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<bool, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<int32_t, Light::SqlRealName { "col_19" }> col19;
    Light::Field<double, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<bool, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<bool, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_29" }> col29;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<&Table59::id, Light::SqlRealName { "fk_59" }, Light::SqlNullable::Null> fk59;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }> fk51;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<&Table54::id, Light::SqlRealName { "fk_54" }> fk54;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<&Table58::id, Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<&Table42::id, Light::SqlRealName { "fk_42" }> fk42;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }, Light::SqlNullable::Null> fk28;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }> fk30;
};

