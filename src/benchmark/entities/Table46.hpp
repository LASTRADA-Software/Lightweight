// File is automatically generated using ddl2cpp.
#pragma once

#include "Table10.hpp"
#include "Table13.hpp"
#include "Table15.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table21.hpp"
#include "Table25.hpp"
#include "Table27.hpp"
#include "Table29.hpp"
#include "Table3.hpp"
#include "Table31.hpp"
#include "Table32.hpp"
#include "Table34.hpp"
#include "Table35.hpp"
#include "Table39.hpp"
#include "Table41.hpp"
#include "Table43.hpp"
#include "Table44.hpp"
#include "Table6.hpp"
#include "Table7.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table46 final
{
    static constexpr std::string_view TableName = "table_46";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<int32_t, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<int32_t, Light::SqlRealName { "col_9" }> col9;
    Light::Field<int32_t, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_11" }> col11;
    Light::Field<int32_t, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<double, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<double, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_29" }> col29;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<Member(Table19::id), Light::SqlRealName { "fk_19" }> fk19;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<Member(Table43::id), Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }> fk13;
    Light::BelongsTo<Member(Table21::id), Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }> fk10;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }, Light::SqlNullable::Null> fk44;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<Member(Table32::id), Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<Member(Table6::id), Light::SqlRealName { "fk_6" }> fk6;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }> fk7;
};

