// File is automatically generated using ddl2cpp.
#pragma once

#include "Table12.hpp"
#include "Table3.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table13 final
{
    static constexpr std::string_view TableName = "table_13";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<double, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_6" }> col6;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
};

