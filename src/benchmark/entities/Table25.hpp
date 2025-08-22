// File is automatically generated using ddl2cpp.
#pragma once

#include "Table11.hpp"
#include "Table14.hpp"
#include "Table17.hpp"
#include "Table2.hpp"
#include "Table22.hpp"
#include "Table23.hpp"
#include "Table3.hpp"
#include "Table4.hpp"
#include "Table5.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table25 final
{
    static constexpr std::string_view TableName = "table_25";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_13" }> col13;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }> fk11;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
};

