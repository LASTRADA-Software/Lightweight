// File is automatically generated using ddl2cpp.
#pragma once

#include "Table11.hpp"
#include "Table16.hpp"
#include "Table17.hpp"
#include "Table18.hpp"
#include "Table21.hpp"
#include "Table22.hpp"
#include "Table27.hpp"
#include "Table29.hpp"
#include "Table34.hpp"
#include "Table40.hpp"
#include "Table8.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table42 final
{
    static constexpr std::string_view TableName = "table_42";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<bool, Light::SqlRealName { "col_9" }> col9;
    Light::Field<bool, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<double, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_19" }> col19;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
};

