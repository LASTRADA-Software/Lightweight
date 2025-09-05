// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table11.hpp"
#include "Table12.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table2.hpp"
#include "Table25.hpp"
#include "Table33.hpp"
#include "Table37.hpp"
#include "Table38.hpp"
#include "Table40.hpp"
#include "Table44.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table56.hpp"
#include "Table59.hpp"
#include "Table65.hpp"
#include "Table67.hpp"
#include "Table68.hpp"
#include "Table7.hpp"
#include "Table71.hpp"
#include "Table79.hpp"
#include "Table8.hpp"
#include "Table81.hpp"
#include "Table82.hpp"
#include "Table86.hpp"
#include "Table94.hpp"
#include "Table98.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table99 final
{
    static constexpr std::string_view TableName = "table_99";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_0" }> col0;
    Light::Field<bool, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<double, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<bool, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_16" }> col16;
    Light::Field<double, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<double, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_22" }> col22;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_23" }> col23;
    Light::Field<int32_t, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_28" }> col28;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }> fk33;
    Light::BelongsTo<Member(Table68::id), Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }> fk25;
    Light::BelongsTo<Member(Table71::id), Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<Member(Table98::id), Light::SqlRealName { "fk_98" }, Light::SqlNullable::Null> fk98;
    Light::BelongsTo<Member(Table37::id), Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<Member(Table40::id), Light::SqlRealName { "fk_40" }> fk40;
    Light::BelongsTo<Member(Table50::id), Light::SqlRealName { "fk_50" }> fk50;
    Light::BelongsTo<Member(Table59::id), Light::SqlRealName { "fk_59" }> fk59;
    Light::BelongsTo<Member(Table81::id), Light::SqlRealName { "fk_81" }> fk81;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<Member(Table82::id), Light::SqlRealName { "fk_82" }> fk82;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }, Light::SqlNullable::Null> fk44;
    Light::BelongsTo<Member(Table86::id), Light::SqlRealName { "fk_86" }> fk86;
    Light::BelongsTo<Member(Table65::id), Light::SqlRealName { "fk_65" }> fk65;
    Light::BelongsTo<Member(Table94::id), Light::SqlRealName { "fk_94" }> fk94;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }> fk11;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }> fk7;
    Light::BelongsTo<Member(Table79::id), Light::SqlRealName { "fk_79" }> fk79;
    Light::BelongsTo<Member(Table38::id), Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<Member(Table56::id), Light::SqlRealName { "fk_56" }, Light::SqlNullable::Null> fk56;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<Member(Table67::id), Light::SqlRealName { "fk_67" }> fk67;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }> fk2;
};

