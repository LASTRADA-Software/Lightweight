// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table10.hpp"
#include "Table11.hpp"
#include "Table12.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table20.hpp"
#include "Table22.hpp"
#include "Table27.hpp"
#include "Table32.hpp"
#include "Table34.hpp"
#include "Table37.hpp"
#include "Table38.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table39 final
{
    static constexpr std::string_view TableName = "table_39";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<double, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_5" }> col5;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }> fk10;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }> fk38;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }> fk37;
};

