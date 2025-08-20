// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table10.hpp"
#include "Table17.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table24.hpp"
#include "Table26.hpp"
#include "Table28.hpp"
#include "Table29.hpp"
#include "Table3.hpp"
#include "Table31.hpp"
#include "Table34.hpp"
#include "Table36.hpp"
#include "Table37.hpp"
#include "Table42.hpp"
#include "Table43.hpp"
#include "Table44.hpp"
#include "Table48.hpp"
#include "Table51.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table54.hpp"
#include "Table55.hpp"
#include "Table56.hpp"
#include "Table57.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table58 final
{
    static constexpr std::string_view TableName = "table_58";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<int32_t, Light::SqlRealName { "col_9" }> col9;
    Light::Field<bool, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<double, Light::SqlRealName { "col_12" }> col12;
    Light::Field<double, Light::SqlRealName { "col_13" }> col13;
    Light::Field<int32_t, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<int32_t, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<int32_t, Light::SqlRealName { "col_19" }> col19;
    Light::Field<int32_t, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<bool, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<double, Light::SqlRealName { "col_30" }> col30;
    Light::Field<double, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<int32_t, Light::SqlRealName { "col_40" }> col40;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<double, Light::SqlRealName { "col_42" }> col42;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_43" }> col43;
    Light::Field<bool, Light::SqlRealName { "col_44" }> col44;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_45" }> col45;
    Light::Field<int32_t, Light::SqlRealName { "col_46" }> col46;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<Member(Table56::id), Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<Member(Table24::id), Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<Member(Table51::id), Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }> fk54;
    Light::BelongsTo<Member(Table42::id), Light::SqlRealName { "fk_42" }> fk42;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<Member(Table48::id), Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<Member(Table52::id), Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<Member(Table37::id), Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<Member(Table43::id), Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<Member(Table17::id), Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }> fk10;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<Member(Table57::id), Light::SqlRealName { "fk_57" }, Light::SqlNullable::Null> fk57;
    Light::BelongsTo<Member(Table18::id), Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<Member(Table19::id), Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<Member(Table28::id), Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<Member(Table26::id), Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }> fk53;
    Light::BelongsTo<Member(Table55::id), Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
};

