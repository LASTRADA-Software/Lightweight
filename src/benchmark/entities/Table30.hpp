// File is automatically generated using ddl2cpp.
#pragma once

#include "Table11.hpp"
#include "Table12.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table22.hpp"
#include "Table24.hpp"
#include "Table27.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table30 final
{
    static constexpr std::string_view TableName = "table_30";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_2" }> col2;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }> fk19;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }> fk24;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }> fk22;
};

