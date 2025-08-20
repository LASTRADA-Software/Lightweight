// File is automatically generated using ddl2cpp.
#pragma once

#include "Table14.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table20.hpp"
#include "Table21.hpp"
#include "Table5.hpp"
#include "Table6.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table24 final
{
    static constexpr std::string_view TableName = "table_24";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<int32_t, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<int32_t, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<double, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_18" }> col18;
    Light::Field<double, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
};

