// File is automatically generated using ddl2cpp.
#pragma once

#include "Table11.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table16.hpp"
#include "Table21.hpp"
#include "Table22.hpp"
#include "Table25.hpp"
#include "Table31.hpp"
#include "Table33.hpp"
#include "Table35.hpp"
#include "Table37.hpp"
#include "Table39.hpp"
#include "Table41.hpp"
#include "Table42.hpp"
#include "Table47.hpp"
#include "Table48.hpp"
#include "Table5.hpp"
#include "Table64.hpp"
#include "Table68.hpp"
#include "Table69.hpp"
#include "Table74.hpp"
#include "Table75.hpp"
#include "Table78.hpp"
#include "Table79.hpp"
#include "Table8.hpp"
#include "Table86.hpp"
#include "Table88.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table92 final
{
    static constexpr std::string_view TableName = "table_92";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<double, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_7" }> col7;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<Member(Table74::id), Light::SqlRealName { "fk_74" }, Light::SqlNullable::Null> fk74;
    Light::BelongsTo<Member(Table78::id), Light::SqlRealName { "fk_78" }, Light::SqlNullable::Null> fk78;
    Light::BelongsTo<Member(Table69::id), Light::SqlRealName { "fk_69" }> fk69;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<Member(Table68::id), Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
    Light::BelongsTo<Member(Table75::id), Light::SqlRealName { "fk_75" }, Light::SqlNullable::Null> fk75;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<Member(Table42::id), Light::SqlRealName { "fk_42" }> fk42;
    Light::BelongsTo<Member(Table86::id), Light::SqlRealName { "fk_86" }> fk86;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<Member(Table21::id), Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<Member(Table37::id), Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<Member(Table48::id), Light::SqlRealName { "fk_48" }> fk48;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }> fk33;
    Light::BelongsTo<Member(Table64::id), Light::SqlRealName { "fk_64" }> fk64;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }> fk11;
    Light::BelongsTo<Member(Table88::id), Light::SqlRealName { "fk_88" }> fk88;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<Member(Table79::id), Light::SqlRealName { "fk_79" }, Light::SqlNullable::Null> fk79;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
};

