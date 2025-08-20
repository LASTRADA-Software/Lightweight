// File is automatically generated using ddl2cpp.
#pragma once

#include "Table18.hpp"
#include "Table25.hpp"
#include "Table30.hpp"
#include "Table33.hpp"
#include "Table41.hpp"
#include "Table44.hpp"
#include "Table46.hpp"
#include "Table51.hpp"
#include "Table66.hpp"
#include "Table7.hpp"
#include "Table74.hpp"
#include "Table78.hpp"
#include "Table88.hpp"
#include "Table91.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table93 final
{
    static constexpr std::string_view TableName = "table_93";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<double, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<bool, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<double, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<int32_t, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<double, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_28" }> col28;
    Light::BelongsTo<Member(Table66::id), Light::SqlRealName { "fk_66" }, Light::SqlNullable::Null> fk66;
    Light::BelongsTo<Member(Table30::id), Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<Member(Table78::id), Light::SqlRealName { "fk_78" }, Light::SqlNullable::Null> fk78;
    Light::BelongsTo<Member(Table74::id), Light::SqlRealName { "fk_74" }, Light::SqlNullable::Null> fk74;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<Member(Table91::id), Light::SqlRealName { "fk_91" }, Light::SqlNullable::Null> fk91;
    Light::BelongsTo<Member(Table88::id), Light::SqlRealName { "fk_88" }, Light::SqlNullable::Null> fk88;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }> fk7;
    Light::BelongsTo<Member(Table46::id), Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<Member(Table51::id), Light::SqlRealName { "fk_51" }> fk51;
    Light::BelongsTo<Member(Table18::id), Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }> fk44;
};

