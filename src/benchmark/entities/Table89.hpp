// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table11.hpp"
#include "Table17.hpp"
#include "Table19.hpp"
#include "Table21.hpp"
#include "Table22.hpp"
#include "Table23.hpp"
#include "Table26.hpp"
#include "Table33.hpp"
#include "Table35.hpp"
#include "Table37.hpp"
#include "Table39.hpp"
#include "Table4.hpp"
#include "Table42.hpp"
#include "Table49.hpp"
#include "Table50.hpp"
#include "Table52.hpp"
#include "Table54.hpp"
#include "Table6.hpp"
#include "Table61.hpp"
#include "Table62.hpp"
#include "Table7.hpp"
#include "Table73.hpp"
#include "Table77.hpp"
#include "Table81.hpp"
#include "Table84.hpp"
#include "Table87.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table89 final
{
    static constexpr std::string_view TableName = "table_89";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<int32_t, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<double, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_19" }> col19;
    Light::BelongsTo<Member(Table49::id), Light::SqlRealName { "fk_49" }, Light::SqlNullable::Null> fk49;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table50::id), Light::SqlRealName { "fk_50" }> fk50;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<Member(Table21::id), Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
    Light::BelongsTo<Member(Table17::id), Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<Member(Table61::id), Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<Member(Table52::id), Light::SqlRealName { "fk_52" }> fk52;
    Light::BelongsTo<Member(Table23::id), Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
    Light::BelongsTo<Member(Table73::id), Light::SqlRealName { "fk_73" }, Light::SqlNullable::Null> fk73;
    Light::BelongsTo<Member(Table0::id), Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }> fk54;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }> fk11;
    Light::BelongsTo<Member(Table62::id), Light::SqlRealName { "fk_62" }> fk62;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }> fk33;
    Light::BelongsTo<Member(Table87::id), Light::SqlRealName { "fk_87" }, Light::SqlNullable::Null> fk87;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<Member(Table26::id), Light::SqlRealName { "fk_26" }, Light::SqlNullable::Null> fk26;
    Light::BelongsTo<Member(Table4::id), Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }> fk7;
    Light::BelongsTo<Member(Table9::id), Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<Member(Table84::id), Light::SqlRealName { "fk_84" }> fk84;
    Light::BelongsTo<Member(Table6::id), Light::SqlRealName { "fk_6" }> fk6;
    Light::BelongsTo<Member(Table19::id), Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<Member(Table37::id), Light::SqlRealName { "fk_37" }> fk37;
    Light::BelongsTo<Member(Table81::id), Light::SqlRealName { "fk_81" }> fk81;
    Light::BelongsTo<Member(Table77::id), Light::SqlRealName { "fk_77" }, Light::SqlNullable::Null> fk77;
    Light::BelongsTo<Member(Table42::id), Light::SqlRealName { "fk_42" }> fk42;
};

