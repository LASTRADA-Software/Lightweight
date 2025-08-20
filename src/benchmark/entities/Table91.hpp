// File is automatically generated using ddl2cpp.
#pragma once

#include "Table14.hpp"
#include "Table20.hpp"
#include "Table28.hpp"
#include "Table31.hpp"
#include "Table35.hpp"
#include "Table37.hpp"
#include "Table38.hpp"
#include "Table4.hpp"
#include "Table40.hpp"
#include "Table48.hpp"
#include "Table5.hpp"
#include "Table51.hpp"
#include "Table54.hpp"
#include "Table58.hpp"
#include "Table6.hpp"
#include "Table65.hpp"
#include "Table69.hpp"
#include "Table7.hpp"
#include "Table71.hpp"
#include "Table75.hpp"
#include "Table76.hpp"
#include "Table77.hpp"
#include "Table79.hpp"
#include "Table82.hpp"
#include "Table83.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table91 final
{
    static constexpr std::string_view TableName = "table_91";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<double, Light::SqlRealName { "col_4" }> col4;
    Light::Field<int32_t, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_11" }> col11;
    Light::Field<bool, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<double, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<bool, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<double, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<int32_t, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<bool, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<double, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_37" }> col37;
    Light::BelongsTo<Member(Table79::id), Light::SqlRealName { "fk_79" }> fk79;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<Member(Table82::id), Light::SqlRealName { "fk_82" }, Light::SqlNullable::Null> fk82;
    Light::BelongsTo<Member(Table38::id), Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<Member(Table75::id), Light::SqlRealName { "fk_75" }> fk75;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<Member(Table65::id), Light::SqlRealName { "fk_65" }, Light::SqlNullable::Null> fk65;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<Member(Table4::id), Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<Member(Table37::id), Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<Member(Table83::id), Light::SqlRealName { "fk_83" }, Light::SqlNullable::Null> fk83;
    Light::BelongsTo<Member(Table77::id), Light::SqlRealName { "fk_77" }> fk77;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<Member(Table6::id), Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<Member(Table48::id), Light::SqlRealName { "fk_48" }> fk48;
    Light::BelongsTo<Member(Table69::id), Light::SqlRealName { "fk_69" }> fk69;
    Light::BelongsTo<Member(Table51::id), Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<Member(Table71::id), Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<Member(Table58::id), Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<Member(Table40::id), Light::SqlRealName { "fk_40" }> fk40;
    Light::BelongsTo<Member(Table20::id), Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<Member(Table28::id), Light::SqlRealName { "fk_28" }, Light::SqlNullable::Null> fk28;
    Light::BelongsTo<Member(Table76::id), Light::SqlRealName { "fk_76" }, Light::SqlNullable::Null> fk76;
};

