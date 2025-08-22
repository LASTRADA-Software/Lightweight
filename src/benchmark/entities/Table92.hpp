// File is automatically generated using ddl2cpp.
#pragma once

#include "Table13.hpp"
#include "Table16.hpp"
#include "Table28.hpp"
#include "Table3.hpp"
#include "Table4.hpp"
#include "Table49.hpp"
#include "Table54.hpp"
#include "Table55.hpp"
#include "Table62.hpp"
#include "Table67.hpp"
#include "Table72.hpp"
#include "Table78.hpp"
#include "Table80.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table92 final
{
    static constexpr std::string_view TableName = "table_92";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<bool, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_27" }> col27;
    Light::Field<int32_t, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<bool, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_33" }> col33;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<&Table80::id, Light::SqlRealName { "fk_80" }, Light::SqlNullable::Null> fk80;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<&Table13::id, Light::SqlRealName { "fk_13" }> fk13;
    Light::BelongsTo<&Table78::id, Light::SqlRealName { "fk_78" }, Light::SqlNullable::Null> fk78;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<&Table72::id, Light::SqlRealName { "fk_72" }, Light::SqlNullable::Null> fk72;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<&Table54::id, Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<&Table67::id, Light::SqlRealName { "fk_67" }> fk67;
};

