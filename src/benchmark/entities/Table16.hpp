// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table10.hpp"
#include "Table12.hpp"
#include "Table14.hpp"
#include "Table2.hpp"
#include "Table3.hpp"
#include "Table7.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table16 final
{
    static constexpr std::string_view TableName = "table_16";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<int32_t, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<int32_t, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_16" }> col16;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table9::id), Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
};

