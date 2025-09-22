// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table13.hpp"
#include "Table17.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table22.hpp"
#include "Table3.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table36.hpp"
#include "Table4.hpp"
#include "Table40.hpp"
#include "Table43.hpp"
#include "Table44.hpp"
#include "Table45.hpp"
#include "Table46.hpp"
#include "Table48.hpp"
#include "Table49.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table54.hpp"
#include "Table56.hpp"
#include "Table58.hpp"
#include "Table60.hpp"
#include "Table61.hpp"
#include "Table62.hpp"
#include "Table63.hpp"
#include "Table66.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table72 final
{
    static constexpr std::string_view TableName = "table_72";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<double, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<double, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<double, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<bool, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_23" }> col23;
    Light::BelongsTo<Member(Table56::id), Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<Member(Table19::id), Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }> fk54;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<Member(Table63::id), Light::SqlRealName { "fk_63" }, Light::SqlNullable::Null> fk63;
    Light::BelongsTo<Member(Table4::id), Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<Member(Table52::id), Light::SqlRealName { "fk_52" }> fk52;
    Light::BelongsTo<Member(Table58::id), Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<Member(Table45::id), Light::SqlRealName { "fk_45" }, Light::SqlNullable::Null> fk45;
    Light::BelongsTo<Member(Table60::id), Light::SqlRealName { "fk_60" }> fk60;
    Light::BelongsTo<Member(Table46::id), Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }, Light::SqlNullable::Null> fk44;
    Light::BelongsTo<Member(Table30::id), Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<Member(Table43::id), Light::SqlRealName { "fk_43" }> fk43;
    Light::BelongsTo<Member(Table62::id), Light::SqlRealName { "fk_62" }> fk62;
    Light::BelongsTo<Member(Table66::id), Light::SqlRealName { "fk_66" }, Light::SqlNullable::Null> fk66;
    Light::BelongsTo<Member(Table49::id), Light::SqlRealName { "fk_49" }, Light::SqlNullable::Null> fk49;
    Light::BelongsTo<Member(Table48::id), Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<Member(Table0::id), Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<Member(Table18::id), Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<Member(Table17::id), Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<Member(Table40::id), Light::SqlRealName { "fk_40" }> fk40;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<Member(Table61::id), Light::SqlRealName { "fk_61" }> fk61;
};

