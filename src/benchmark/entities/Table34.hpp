// File is automatically generated using ddl2cpp.
#pragma once

#include "Table10.hpp"
#include "Table28.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table34 final
{
    static constexpr std::string_view TableName = "table_34";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_3" }> col3;
    Light::BelongsTo<Member(Table28::id), Light::SqlRealName { "fk_28" }, Light::SqlNullable::Null> fk28;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
};

