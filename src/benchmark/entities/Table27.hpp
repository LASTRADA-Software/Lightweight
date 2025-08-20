// File is automatically generated using ddl2cpp.
#pragma once

#include "Table14.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table27 final
{
    static constexpr std::string_view TableName = "table_27";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_3" }> col3;
    Light::Field<double, Light::SqlRealName { "col_4" }> col4;
    Light::Field<int32_t, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_7" }> col7;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
};

