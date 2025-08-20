// File is automatically generated using ddl2cpp.
#pragma once

#include "Table17.hpp"
#include "Table22.hpp"
#include "Table24.hpp"
#include "Table28.hpp"
#include "Table36.hpp"
#include "Table4.hpp"
#include "Table43.hpp"
#include "Table5.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table45 final
{
    static constexpr std::string_view TableName = "table_45";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_2" }> col2;
    Light::Field<bool, Light::SqlRealName { "col_3" }> col3;
    Light::Field<bool, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<double, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_18" }> col18;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }> fk43;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
};

