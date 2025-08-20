// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table12.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table18.hpp"
#include "Table27.hpp"
#include "Table30.hpp"
#include "Table34.hpp"
#include "Table39.hpp"
#include "Table40.hpp"
#include "Table41.hpp"
#include "Table44.hpp"
#include "Table45.hpp"
#include "Table46.hpp"
#include "Table47.hpp"
#include "Table5.hpp"
#include "Table51.hpp"
#include "Table53.hpp"
#include "Table55.hpp"
#include "Table57.hpp"
#include "Table62.hpp"
#include "Table68.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table71 final
{
    static constexpr std::string_view TableName = "table_71";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<double, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<double, Light::SqlRealName { "col_7" }> col7;
    Light::Field<int32_t, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<double, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<double, Light::SqlRealName { "col_23" }> col23;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<Member(Table40::id), Light::SqlRealName { "fk_40" }> fk40;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<Member(Table30::id), Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<Member(Table57::id), Light::SqlRealName { "fk_57" }> fk57;
    Light::BelongsTo<Member(Table62::id), Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<Member(Table46::id), Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<Member(Table55::id), Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<Member(Table18::id), Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<Member(Table51::id), Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<Member(Table45::id), Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<Member(Table68::id), Light::SqlRealName { "fk_68" }> fk68;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
};

