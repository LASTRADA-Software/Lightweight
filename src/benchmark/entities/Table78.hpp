// File is automatically generated using ddl2cpp.
#pragma once

#include "Table18.hpp"
#include "Table31.hpp"
#include "Table44.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table78 final
{
    static constexpr std::string_view TableName = "table_78";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<double, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<bool, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_11" }> col11;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
};

