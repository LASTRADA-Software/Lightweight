// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table13.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table21.hpp"
#include "Table27.hpp"
#include "Table29.hpp"
#include "Table31.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table36.hpp"
#include "Table38.hpp"
#include "Table39.hpp"
#include "Table45.hpp"
#include "Table46.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table53.hpp"
#include "Table54.hpp"
#include "Table55.hpp"
#include "Table57.hpp"
#include "Table59.hpp"
#include "Table6.hpp"
#include "Table60.hpp"
#include "Table61.hpp"
#include "Table62.hpp"
#include "Table65.hpp"
#include "Table7.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table66 final
{
    static constexpr std::string_view TableName = "table_66";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<double, Light::SqlRealName { "col_5" }> col5;
    Light::Field<int32_t, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<int32_t, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_11" }> col11;
    Light::BelongsTo<Member(Table60::id), Light::SqlRealName { "fk_60" }> fk60;
    Light::BelongsTo<Member(Table57::id), Light::SqlRealName { "fk_57" }> fk57;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }> fk53;
    Light::BelongsTo<Member(Table18::id), Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table59::id), Light::SqlRealName { "fk_59" }> fk59;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<Member(Table45::id), Light::SqlRealName { "fk_45" }, Light::SqlNullable::Null> fk45;
    Light::BelongsTo<Member(Table55::id), Light::SqlRealName { "fk_55" }, Light::SqlNullable::Null> fk55;
    Light::BelongsTo<Member(Table9::id), Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<Member(Table61::id), Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<Member(Table62::id), Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<Member(Table13::id), Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<Member(Table19::id), Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<Member(Table21::id), Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<Member(Table50::id), Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<Member(Table65::id), Light::SqlRealName { "fk_65" }, Light::SqlNullable::Null> fk65;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<Member(Table6::id), Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }> fk54;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<Member(Table38::id), Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<Member(Table46::id), Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
};

