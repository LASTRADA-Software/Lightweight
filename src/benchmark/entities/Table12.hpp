// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table12 final
{
    static constexpr std::string_view TableName = "table_12";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<double, Light::SqlRealName { "col_4" }> col4;
    Light::Field<bool, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_10" }> col10;
    Light::BelongsTo<Member(Table0::id), Light::SqlRealName { "fk_0" }> fk0;
};

