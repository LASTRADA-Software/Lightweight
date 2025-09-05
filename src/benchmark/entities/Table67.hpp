// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table15.hpp"
#include "Table27.hpp"
#include "Table42.hpp"
#include "Table45.hpp"
#include "Table46.hpp"
#include "Table47.hpp"
#include "Table5.hpp"
#include "Table54.hpp"
#include "Table64.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table67 final
{
    static constexpr std::string_view TableName = "table_67";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<bool, Light::SqlRealName { "col_4" }> col4;
    Light::Field<bool, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<double, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_16" }> col16;
    Light::Field<double, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<bool, Light::SqlRealName { "col_22" }> col22;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<double, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<int32_t, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<bool, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<double, Light::SqlRealName { "col_42" }> col42;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_43" }> col43;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_44" }> col44;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_45" }> col45;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_46" }> col46;
    Light::BelongsTo<Member(Table46::id), Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<Member(Table45::id), Light::SqlRealName { "fk_45" }, Light::SqlNullable::Null> fk45;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<Member(Table64::id), Light::SqlRealName { "fk_64" }, Light::SqlNullable::Null> fk64;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }> fk54;
    Light::BelongsTo<Member(Table42::id), Light::SqlRealName { "fk_42" }, Light::SqlNullable::Null> fk42;
    Light::BelongsTo<Member(Table9::id), Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
};

