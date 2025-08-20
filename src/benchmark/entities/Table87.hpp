// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table10.hpp"
#include "Table11.hpp"
#include "Table12.hpp"
#include "Table16.hpp"
#include "Table18.hpp"
#include "Table23.hpp"
#include "Table28.hpp"
#include "Table29.hpp"
#include "Table3.hpp"
#include "Table30.hpp"
#include "Table32.hpp"
#include "Table36.hpp"
#include "Table37.hpp"
#include "Table38.hpp"
#include "Table39.hpp"
#include "Table47.hpp"
#include "Table56.hpp"
#include "Table6.hpp"
#include "Table61.hpp"
#include "Table62.hpp"
#include "Table63.hpp"
#include "Table64.hpp"
#include "Table7.hpp"
#include "Table74.hpp"
#include "Table76.hpp"
#include "Table77.hpp"
#include "Table81.hpp"
#include "Table82.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table87 final
{
    static constexpr std::string_view TableName = "table_87";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<bool, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<double, Light::SqlRealName { "col_19" }> col19;
    Light::Field<bool, Light::SqlRealName { "col_20" }> col20;
    Light::Field<double, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_22" }> col22;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<&Table64::id, Light::SqlRealName { "fk_64" }> fk64;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }> fk10;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }> fk47;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<&Table82::id, Light::SqlRealName { "fk_82" }> fk82;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }> fk38;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
    Light::BelongsTo<&Table63::id, Light::SqlRealName { "fk_63" }> fk63;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<&Table61::id, Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<&Table77::id, Light::SqlRealName { "fk_77" }, Light::SqlNullable::Null> fk77;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table74::id, Light::SqlRealName { "fk_74" }> fk74;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<&Table76::id, Light::SqlRealName { "fk_76" }> fk76;
    Light::BelongsTo<&Table81::id, Light::SqlRealName { "fk_81" }, Light::SqlNullable::Null> fk81;
};

