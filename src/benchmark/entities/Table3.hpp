// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table2.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table3 final
{
    static constexpr std::string_view TableName = "table_3";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<int32_t, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_2" }> col2;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
};

