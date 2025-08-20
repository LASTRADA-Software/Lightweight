// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table12.hpp"
#include "Table14.hpp"
#include "Table17.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table21.hpp"
#include "Table23.hpp"
#include "Table25.hpp"
#include "Table30.hpp"
#include "Table34.hpp"
#include "Table35.hpp"
#include "Table37.hpp"
#include "Table38.hpp"
#include "Table39.hpp"
#include "Table40.hpp"
#include "Table43.hpp"
#include "Table46.hpp"
#include "Table47.hpp"
#include "Table49.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table55.hpp"
#include "Table56.hpp"
#include "Table6.hpp"
#include "Table7.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table57 final
{
    static constexpr std::string_view TableName = "table_57";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<int32_t, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<double, Light::SqlRealName { "col_9" }> col9;
    Light::Field<double, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<double, Light::SqlRealName { "col_18" }> col18;
    Light::Field<bool, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<double, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_34" }> col34;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }> fk50;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<&Table25::id, Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table53::id, Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
};

