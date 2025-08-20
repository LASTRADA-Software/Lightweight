// File is automatically generated using ddl2cpp.
#pragma once

#include "Table13.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table31 final
{
    static constexpr std::string_view TableName = "table_31";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_2" }> col2;
    Light::BelongsTo<&Table13::id, Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
};

