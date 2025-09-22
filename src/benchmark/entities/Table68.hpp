// File is automatically generated using ddl2cpp.
#pragma once

#include "Table11.hpp"
#include "Table12.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table16.hpp"
#include "Table17.hpp"
#include "Table2.hpp"
#include "Table22.hpp"
#include "Table23.hpp"
#include "Table24.hpp"
#include "Table27.hpp"
#include "Table3.hpp"
#include "Table31.hpp"
#include "Table34.hpp"
#include "Table36.hpp"
#include "Table41.hpp"
#include "Table44.hpp"
#include "Table46.hpp"
#include "Table50.hpp"
#include "Table53.hpp"
#include "Table55.hpp"
#include "Table60.hpp"
#include "Table62.hpp"
#include "Table64.hpp"
#include "Table65.hpp"
#include "Table7.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table68 final
{
    static constexpr std::string_view TableName = "table_68";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<int32_t, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<double, Light::SqlRealName { "col_11" }> col11;
    Light::Field<double, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<double, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_17" }> col17;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }> fk53;
    Light::BelongsTo<Member(Table65::id), Light::SqlRealName { "fk_65" }> fk65;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<Member(Table55::id), Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<Member(Table23::id), Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<Member(Table9::id), Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<Member(Table17::id), Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }, Light::SqlNullable::Null> fk44;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }> fk11;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<Member(Table24::id), Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<Member(Table50::id), Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<Member(Table46::id), Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<Member(Table60::id), Light::SqlRealName { "fk_60" }> fk60;
    Light::BelongsTo<Member(Table64::id), Light::SqlRealName { "fk_64" }, Light::SqlNullable::Null> fk64;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<Member(Table62::id), Light::SqlRealName { "fk_62" }> fk62;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }> fk13;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }> fk12;
};

