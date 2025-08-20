// File is automatically generated using ddl2cpp.
#pragma once

#include "Table22.hpp"
#include "Table3.hpp"
#include "Table34.hpp"
#include "Table35.hpp"
#include "Table38.hpp"
#include "Table40.hpp"
#include "Table43.hpp"
#include "Table44.hpp"
#include "Table47.hpp"
#include "Table51.hpp"
#include "Table54.hpp"
#include "Table56.hpp"
#include "Table57.hpp"
#include "Table62.hpp"
#include "Table65.hpp"
#include "Table68.hpp"
#include "Table72.hpp"
#include "Table74.hpp"
#include "Table8.hpp"
#include "Table83.hpp"
#include "Table84.hpp"
#include "Table90.hpp"
#include "Table93.hpp"
#include "Table94.hpp"
#include "Table96.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table98 final
{
    static constexpr std::string_view TableName = "table_98";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<int32_t, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<int32_t, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_10" }> col10;
    Light::Field<int32_t, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<int32_t, Light::SqlRealName { "col_22" }> col22;
    Light::Field<int32_t, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_26" }> col26;
    Light::Field<double, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_35" }> col35;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_36" }> col36;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<double, Light::SqlRealName { "col_38" }> col38;
    Light::Field<bool, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_42" }> col42;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_43" }> col43;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<Member(Table56::id), Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<Member(Table94::id), Light::SqlRealName { "fk_94" }, Light::SqlNullable::Null> fk94;
    Light::BelongsTo<Member(Table74::id), Light::SqlRealName { "fk_74" }> fk74;
    Light::BelongsTo<Member(Table68::id), Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
    Light::BelongsTo<Member(Table84::id), Light::SqlRealName { "fk_84" }, Light::SqlNullable::Null> fk84;
    Light::BelongsTo<Member(Table72::id), Light::SqlRealName { "fk_72" }> fk72;
    Light::BelongsTo<Member(Table57::id), Light::SqlRealName { "fk_57" }, Light::SqlNullable::Null> fk57;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<Member(Table62::id), Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }> fk54;
    Light::BelongsTo<Member(Table65::id), Light::SqlRealName { "fk_65" }, Light::SqlNullable::Null> fk65;
    Light::BelongsTo<Member(Table83::id), Light::SqlRealName { "fk_83" }> fk83;
    Light::BelongsTo<Member(Table96::id), Light::SqlRealName { "fk_96" }> fk96;
    Light::BelongsTo<Member(Table38::id), Light::SqlRealName { "fk_38" }> fk38;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<Member(Table40::id), Light::SqlRealName { "fk_40" }> fk40;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<Member(Table43::id), Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<Member(Table90::id), Light::SqlRealName { "fk_90" }> fk90;
    Light::BelongsTo<Member(Table93::id), Light::SqlRealName { "fk_93" }, Light::SqlNullable::Null> fk93;
    Light::BelongsTo<Member(Table51::id), Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
};

