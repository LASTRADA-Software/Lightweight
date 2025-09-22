// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table15.hpp"
#include "Table18.hpp"
#include "Table21.hpp"
#include "Table25.hpp"
#include "Table44.hpp"
#include "Table45.hpp"
#include "Table64.hpp"
#include "Table68.hpp"
#include "Table75.hpp"
#include "Table94.hpp"
#include "Table96.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table97 final
{
    static constexpr std::string_view TableName = "table_97";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_0" }> col0;
    Light::Field<bool, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<bool, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<double, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_12" }> col12;
    Light::BelongsTo<Member(Table21::id), Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
    Light::BelongsTo<Member(Table75::id), Light::SqlRealName { "fk_75" }, Light::SqlNullable::Null> fk75;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<Member(Table96::id), Light::SqlRealName { "fk_96" }, Light::SqlNullable::Null> fk96;
    Light::BelongsTo<Member(Table94::id), Light::SqlRealName { "fk_94" }> fk94;
    Light::BelongsTo<Member(Table68::id), Light::SqlRealName { "fk_68" }> fk68;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table45::id), Light::SqlRealName { "fk_45" }, Light::SqlNullable::Null> fk45;
    Light::BelongsTo<Member(Table64::id), Light::SqlRealName { "fk_64" }, Light::SqlNullable::Null> fk64;
    Light::BelongsTo<Member(Table18::id), Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }> fk25;
};

