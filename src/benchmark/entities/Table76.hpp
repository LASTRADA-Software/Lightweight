// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table10.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table21.hpp"
#include "Table35.hpp"
#include "Table37.hpp"
#include "Table4.hpp"
#include "Table41.hpp"
#include "Table44.hpp"
#include "Table50.hpp"
#include "Table51.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table62.hpp"
#include "Table68.hpp"
#include "Table70.hpp"
#include "Table71.hpp"
#include "Table72.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table76 final
{
    static constexpr std::string_view TableName = "table_76";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<bool, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<double, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<double, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<double, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_35" }> col35;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<&Table72::id, Light::SqlRealName { "fk_72" }> fk72;
    Light::BelongsTo<&Table68::id, Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }> fk51;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<&Table70::id, Light::SqlRealName { "fk_70" }, Light::SqlNullable::Null> fk70;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }> fk10;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<&Table53::id, Light::SqlRealName { "fk_53" }> fk53;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }> fk52;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }> fk44;
};

