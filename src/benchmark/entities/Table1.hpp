// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table1 final
{
    static constexpr std::string_view TableName = "table_1";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<int32_t, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_8" }> col8;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
};

