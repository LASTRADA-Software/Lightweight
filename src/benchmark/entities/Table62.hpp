// File is automatically generated using ddl2cpp.
#pragma once

#include "Table14.hpp"
#include "Table17.hpp"
#include "Table18.hpp"
#include "Table2.hpp"
#include "Table21.hpp"
#include "Table23.hpp"
#include "Table24.hpp"
#include "Table29.hpp"
#include "Table30.hpp"
#include "Table32.hpp"
#include "Table34.hpp"
#include "Table39.hpp"
#include "Table41.hpp"
#include "Table42.hpp"
#include "Table43.hpp"
#include "Table45.hpp"
#include "Table48.hpp"
#include "Table49.hpp"
#include "Table52.hpp"
#include "Table56.hpp"
#include "Table57.hpp"
#include "Table58.hpp"
#include "Table61.hpp"
#include "Table7.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table62 final
{
    static constexpr std::string_view TableName = "table_62";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<double, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_10" }> col10;
    Light::Field<double, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<double, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_25" }> col25;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }> fk48;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<&Table61::id, Light::SqlRealName { "fk_61" }> fk61;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }> fk7;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }> fk43;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }, Light::SqlNullable::Null> fk45;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<&Table42::id, Light::SqlRealName { "fk_42" }, Light::SqlNullable::Null> fk42;
    Light::BelongsTo<&Table57::id, Light::SqlRealName { "fk_57" }> fk57;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table58::id, Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
};

