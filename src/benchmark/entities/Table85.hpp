// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table12.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table2.hpp"
#include "Table29.hpp"
#include "Table36.hpp"
#include "Table38.hpp"
#include "Table45.hpp"
#include "Table47.hpp"
#include "Table54.hpp"
#include "Table55.hpp"
#include "Table57.hpp"
#include "Table58.hpp"
#include "Table6.hpp"
#include "Table67.hpp"
#include "Table68.hpp"
#include "Table69.hpp"
#include "Table74.hpp"
#include "Table75.hpp"
#include "Table79.hpp"
#include "Table80.hpp"
#include "Table81.hpp"
#include "Table82.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table85 final
{
    static constexpr std::string_view TableName = "table_85";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_6" }> col6;
    Light::Field<bool, Light::SqlRealName { "col_7" }> col7;
    Light::Field<double, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_18" }> col18;
    Light::Field<bool, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<bool, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_42" }> col42;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_43" }> col43;
    Light::Field<int32_t, Light::SqlRealName { "col_44" }> col44;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_45" }> col45;
    Light::BelongsTo<Member(Table45::id), Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<Member(Table67::id), Light::SqlRealName { "fk_67" }> fk67;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<Member(Table75::id), Light::SqlRealName { "fk_75" }> fk75;
    Light::BelongsTo<Member(Table58::id), Light::SqlRealName { "fk_58" }> fk58;
    Light::BelongsTo<Member(Table55::id), Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<Member(Table38::id), Light::SqlRealName { "fk_38" }> fk38;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<Member(Table57::id), Light::SqlRealName { "fk_57" }, Light::SqlNullable::Null> fk57;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<Member(Table74::id), Light::SqlRealName { "fk_74" }> fk74;
    Light::BelongsTo<Member(Table79::id), Light::SqlRealName { "fk_79" }, Light::SqlNullable::Null> fk79;
    Light::BelongsTo<Member(Table69::id), Light::SqlRealName { "fk_69" }, Light::SqlNullable::Null> fk69;
    Light::BelongsTo<Member(Table81::id), Light::SqlRealName { "fk_81" }, Light::SqlNullable::Null> fk81;
    Light::BelongsTo<Member(Table80::id), Light::SqlRealName { "fk_80" }> fk80;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<Member(Table82::id), Light::SqlRealName { "fk_82" }> fk82;
    Light::BelongsTo<Member(Table6::id), Light::SqlRealName { "fk_6" }> fk6;
    Light::BelongsTo<Member(Table68::id), Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
};

