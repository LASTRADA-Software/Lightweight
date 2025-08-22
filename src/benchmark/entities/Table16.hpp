// File is automatically generated using ddl2cpp.
#pragma once

#include "Table11.hpp"
#include "Table5.hpp"
#include "Table7.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table16 final
{
    static constexpr std::string_view TableName = "table_16";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_3" }> col3;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }> fk7;
};

