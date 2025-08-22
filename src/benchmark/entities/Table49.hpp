// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table10.hpp"
#include "Table14.hpp"
#include "Table16.hpp"
#include "Table18.hpp"
#include "Table2.hpp"
#include "Table23.hpp"
#include "Table24.hpp"
#include "Table26.hpp"
#include "Table28.hpp"
#include "Table3.hpp"
#include "Table32.hpp"
#include "Table34.hpp"
#include "Table36.hpp"
#include "Table37.hpp"
#include "Table38.hpp"
#include "Table39.hpp"
#include "Table4.hpp"
#include "Table40.hpp"
#include "Table41.hpp"
#include "Table42.hpp"
#include "Table43.hpp"
#include "Table45.hpp"
#include "Table46.hpp"
#include "Table6.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table49 final
{
    static constexpr std::string_view TableName = "table_49";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<double, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_2" }> col2;
    Light::Field<double, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<int32_t, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_11" }> col11;
    Light::Field<double, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<int32_t, Light::SqlRealName { "col_14" }> col14;
    Light::Field<bool, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<double, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<double, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<double, Light::SqlRealName { "col_26" }> col26;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<double, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_42" }> col42;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_43" }> col43;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_44" }> col44;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<&Table42::id, Light::SqlRealName { "fk_42" }, Light::SqlNullable::Null> fk42;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }, Light::SqlNullable::Null> fk45;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }> fk24;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }, Light::SqlNullable::Null> fk39;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }> fk43;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<&Table26::id, Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
};

