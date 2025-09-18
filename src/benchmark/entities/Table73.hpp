// File is automatically generated using ddl2cpp.
#pragma once

#include "Table10.hpp"
#include "Table12.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table21.hpp"
#include "Table26.hpp"
#include "Table27.hpp"
#include "Table29.hpp"
#include "Table30.hpp"
#include "Table32.hpp"
#include "Table40.hpp"
#include "Table43.hpp"
#include "Table44.hpp"
#include "Table46.hpp"
#include "Table5.hpp"
#include "Table56.hpp"
#include "Table58.hpp"
#include "Table64.hpp"
#include "Table66.hpp"
#include "Table7.hpp"
#include "Table70.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table73 final
{
    static constexpr std::string_view TableName = "table_73";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<double, Light::SqlRealName { "col_0" }> col0;
    Light::Field<int32_t, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<int32_t, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<double, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_22" }> col22;
    Light::BelongsTo<Member(Table40::id), Light::SqlRealName { "fk_40" }> fk40;
    Light::BelongsTo<Member(Table30::id), Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }> fk13;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<Member(Table70::id), Light::SqlRealName { "fk_70" }, Light::SqlNullable::Null> fk70;
    Light::BelongsTo<Member(Table58::id), Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }> fk7;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<Member(Table56::id), Light::SqlRealName { "fk_56" }, Light::SqlNullable::Null> fk56;
    Light::BelongsTo<Member(Table46::id), Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<Member(Table32::id), Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<Member(Table64::id), Light::SqlRealName { "fk_64" }> fk64;
    Light::BelongsTo<Member(Table26::id), Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<Member(Table21::id), Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
    Light::BelongsTo<Member(Table43::id), Light::SqlRealName { "fk_43" }> fk43;
    Light::BelongsTo<Member(Table66::id), Light::SqlRealName { "fk_66" }> fk66;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
};

