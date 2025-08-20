// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table11.hpp"
#include "Table12.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table17.hpp"
#include "Table19.hpp"
#include "Table23.hpp"
#include "Table27.hpp"
#include "Table29.hpp"
#include "Table31.hpp"
#include "Table34.hpp"
#include "Table36.hpp"
#include "Table38.hpp"
#include "Table39.hpp"
#include "Table4.hpp"
#include "Table7.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table48 final
{
    static constexpr std::string_view TableName = "table_48";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_5" }> col5;
    Light::Field<int32_t, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<bool, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_26" }> col26;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<Member(Table23::id), Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<Member(Table19::id), Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<Member(Table4::id), Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table17::id), Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<Member(Table38::id), Light::SqlRealName { "fk_38" }> fk38;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }> fk29;
};

