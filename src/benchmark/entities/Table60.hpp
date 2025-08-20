// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table10.hpp"
#include "Table11.hpp"
#include "Table12.hpp"
#include "Table16.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table26.hpp"
#include "Table35.hpp"
#include "Table41.hpp"
#include "Table53.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table60 final
{
    static constexpr std::string_view TableName = "table_60";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<double, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<bool, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_11" }> col11;
    Light::Field<int32_t, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_15" }> col15;
    Light::BelongsTo<Member(Table20::id), Light::SqlRealName { "fk_20" }> fk20;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }> fk10;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }> fk53;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<Member(Table26::id), Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
};

