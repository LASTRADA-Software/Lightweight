// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table12.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table21.hpp"
#include "Table31.hpp"
#include "Table32.hpp"
#include "Table35.hpp"
#include "Table36.hpp"
#include "Table38.hpp"
#include "Table44.hpp"
#include "Table46.hpp"
#include "Table50.hpp"
#include "Table53.hpp"
#include "Table54.hpp"
#include "Table55.hpp"
#include "Table57.hpp"
#include "Table6.hpp"
#include "Table60.hpp"
#include "Table61.hpp"
#include "Table64.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table67 final
{
    static constexpr std::string_view TableName = "table_67";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<double, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_15" }> col15;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<&Table53::id, Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<&Table61::id, Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<&Table57::id, Light::SqlRealName { "fk_57" }, Light::SqlNullable::Null> fk57;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<&Table13::id, Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<&Table60::id, Light::SqlRealName { "fk_60" }, Light::SqlNullable::Null> fk60;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }> fk50;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<&Table54::id, Light::SqlRealName { "fk_54" }> fk54;
    Light::BelongsTo<&Table64::id, Light::SqlRealName { "fk_64" }> fk64;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }> fk14;
};

