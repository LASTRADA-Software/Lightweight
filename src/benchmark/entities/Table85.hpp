// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table10.hpp"
#include "Table17.hpp"
#include "Table22.hpp"
#include "Table3.hpp"
#include "Table32.hpp"
#include "Table33.hpp"
#include "Table36.hpp"
#include "Table38.hpp"
#include "Table4.hpp"
#include "Table45.hpp"
#include "Table46.hpp"
#include "Table48.hpp"
#include "Table49.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table55.hpp"
#include "Table59.hpp"
#include "Table60.hpp"
#include "Table64.hpp"
#include "Table65.hpp"
#include "Table66.hpp"
#include "Table70.hpp"
#include "Table72.hpp"
#include "Table73.hpp"
#include "Table74.hpp"
#include "Table77.hpp"
#include "Table78.hpp"
#include "Table82.hpp"
#include "Table83.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table85 final
{
    static constexpr std::string_view TableName = "table_85";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<double, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<double, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<double, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<bool, Light::SqlRealName { "col_16" }> col16;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<&Table66::id, Light::SqlRealName { "fk_66" }, Light::SqlNullable::Null> fk66;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table59::id, Light::SqlRealName { "fk_59" }> fk59;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<&Table72::id, Light::SqlRealName { "fk_72" }> fk72;
    Light::BelongsTo<&Table74::id, Light::SqlRealName { "fk_74" }> fk74;
    Light::BelongsTo<&Table70::id, Light::SqlRealName { "fk_70" }> fk70;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }> fk10;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }, Light::SqlNullable::Null> fk49;
    Light::BelongsTo<&Table33::id, Light::SqlRealName { "fk_33" }> fk33;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<&Table77::id, Light::SqlRealName { "fk_77" }, Light::SqlNullable::Null> fk77;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table65::id, Light::SqlRealName { "fk_65" }> fk65;
    Light::BelongsTo<&Table60::id, Light::SqlRealName { "fk_60" }> fk60;
    Light::BelongsTo<&Table73::id, Light::SqlRealName { "fk_73" }> fk73;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<&Table64::id, Light::SqlRealName { "fk_64" }> fk64;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<&Table78::id, Light::SqlRealName { "fk_78" }> fk78;
    Light::BelongsTo<&Table82::id, Light::SqlRealName { "fk_82" }> fk82;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table83::id, Light::SqlRealName { "fk_83" }> fk83;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }> fk32;
};

