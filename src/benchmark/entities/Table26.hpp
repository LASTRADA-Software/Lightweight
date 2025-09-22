// File is automatically generated using ddl2cpp.
#pragma once

#include "Table22.hpp"
#include "Table23.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table26 final
{
    static constexpr std::string_view TableName = "table_26";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<int32_t, Light::SqlRealName { "col_2" }> col2;
    Light::Field<int32_t, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_6" }> col6;
    Light::BelongsTo<Member(Table23::id), Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }> fk22;
};

