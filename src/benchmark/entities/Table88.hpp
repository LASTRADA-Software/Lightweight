// File is automatically generated using ddl2cpp.
#pragma once

#include "Table10.hpp"
#include "Table11.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table2.hpp"
#include "Table21.hpp"
#include "Table32.hpp"
#include "Table34.hpp"
#include "Table38.hpp"
#include "Table4.hpp"
#include "Table40.hpp"
#include "Table42.hpp"
#include "Table45.hpp"
#include "Table47.hpp"
#include "Table49.hpp"
#include "Table52.hpp"
#include "Table56.hpp"
#include "Table62.hpp"
#include "Table63.hpp"
#include "Table69.hpp"
#include "Table75.hpp"
#include "Table77.hpp"
#include "Table80.hpp"
#include "Table84.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table88 final
{
    static constexpr std::string_view TableName = "table_88";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<int32_t, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<bool, Light::SqlRealName { "col_18" }> col18;
    Light::Field<double, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_21" }> col21;
    Light::Field<double, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_32" }> col32;
    Light::Field<int32_t, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_35" }> col35;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_39" }> col39;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<Member(Table80::id), Light::SqlRealName { "fk_80" }> fk80;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<Member(Table77::id), Light::SqlRealName { "fk_77" }, Light::SqlNullable::Null> fk77;
    Light::BelongsTo<Member(Table40::id), Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<Member(Table75::id), Light::SqlRealName { "fk_75" }, Light::SqlNullable::Null> fk75;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }> fk10;
    Light::BelongsTo<Member(Table21::id), Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
    Light::BelongsTo<Member(Table38::id), Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<Member(Table56::id), Light::SqlRealName { "fk_56" }, Light::SqlNullable::Null> fk56;
    Light::BelongsTo<Member(Table32::id), Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<Member(Table52::id), Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<Member(Table62::id), Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<Member(Table84::id), Light::SqlRealName { "fk_84" }> fk84;
    Light::BelongsTo<Member(Table63::id), Light::SqlRealName { "fk_63" }, Light::SqlNullable::Null> fk63;
    Light::BelongsTo<Member(Table69::id), Light::SqlRealName { "fk_69" }> fk69;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<Member(Table9::id), Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<Member(Table42::id), Light::SqlRealName { "fk_42" }, Light::SqlNullable::Null> fk42;
    Light::BelongsTo<Member(Table45::id), Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<Member(Table4::id), Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<Member(Table49::id), Light::SqlRealName { "fk_49" }> fk49;
};

