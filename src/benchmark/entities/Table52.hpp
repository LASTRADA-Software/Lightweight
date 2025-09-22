// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table10.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table16.hpp"
#include "Table18.hpp"
#include "Table21.hpp"
#include "Table24.hpp"
#include "Table26.hpp"
#include "Table29.hpp"
#include "Table3.hpp"
#include "Table31.hpp"
#include "Table33.hpp"
#include "Table35.hpp"
#include "Table37.hpp"
#include "Table39.hpp"
#include "Table40.hpp"
#include "Table41.hpp"
#include "Table42.hpp"
#include "Table48.hpp"
#include "Table5.hpp"
#include "Table51.hpp"
#include "Table6.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table52 final
{
    static constexpr std::string_view TableName = "table_52";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_1" }> col1;
    Light::Field<bool, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_3" }> col3;
    Light::Field<double, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<double, Light::SqlRealName { "col_7" }> col7;
    Light::Field<double, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<double, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<double, Light::SqlRealName { "col_21" }> col21;
    Light::Field<bool, Light::SqlRealName { "col_22" }> col22;
    Light::Field<bool, Light::SqlRealName { "col_23" }> col23;
    Light::Field<int32_t, Light::SqlRealName { "col_24" }> col24;
    Light::Field<int32_t, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<double, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<bool, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<bool, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<bool, Light::SqlRealName { "col_41" }> col41;
    Light::BelongsTo<Member(Table24::id), Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<Member(Table0::id), Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<Member(Table40::id), Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<Member(Table42::id), Light::SqlRealName { "fk_42" }, Light::SqlNullable::Null> fk42;
    Light::BelongsTo<Member(Table6::id), Light::SqlRealName { "fk_6" }> fk6;
    Light::BelongsTo<Member(Table37::id), Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<Member(Table51::id), Light::SqlRealName { "fk_51" }> fk51;
    Light::BelongsTo<Member(Table18::id), Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<Member(Table21::id), Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<Member(Table26::id), Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }> fk33;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }, Light::SqlNullable::Null> fk39;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<Member(Table48::id), Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
};

