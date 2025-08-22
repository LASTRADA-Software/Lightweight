// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table12.hpp"
#include "Table13.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table17.hpp"
#include "Table18.hpp"
#include "Table22.hpp"
#include "Table23.hpp"
#include "Table24.hpp"
#include "Table26.hpp"
#include "Table28.hpp"
#include "Table31.hpp"
#include "Table32.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table37.hpp"
#include "Table38.hpp"
#include "Table39.hpp"
#include "Table6.hpp"
#include "Table8.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table41 final
{
    static constexpr std::string_view TableName = "table_41";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<int32_t, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<int32_t, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_10" }> col10;
    Light::Field<bool, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_17" }> col17;
    Light::Field<int32_t, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_19" }> col19;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }, Light::SqlNullable::Null> fk28;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }, Light::SqlNullable::Null> fk39;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table33::id, Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<&Table26::id, Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<&Table13::id, Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }> fk24;
};

