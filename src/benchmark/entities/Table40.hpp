// File is automatically generated using ddl2cpp.
#pragma once

#include "Table12.hpp"
#include "Table16.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table23.hpp"
#include "Table24.hpp"
#include "Table26.hpp"
#include "Table3.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table5.hpp"
#include "Table7.hpp"
#include "Table8.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table40 final
{
    static constexpr std::string_view TableName = "table_40";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_2" }> col2;
    Light::Field<bool, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<int32_t, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<bool, Light::SqlRealName { "col_22" }> col22;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<bool, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<bool, Light::SqlRealName { "col_32" }> col32;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_37" }> col37;
    Light::BelongsTo<Member(Table24::id), Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<Member(Table20::id), Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<Member(Table9::id), Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<Member(Table26::id), Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<Member(Table23::id), Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
};

