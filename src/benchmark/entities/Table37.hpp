// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table10.hpp"
#include "Table11.hpp"
#include "Table12.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table18.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table22.hpp"
#include "Table23.hpp"
#include "Table25.hpp"
#include "Table27.hpp"
#include "Table3.hpp"
#include "Table31.hpp"
#include "Table32.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table36.hpp"
#include "Table4.hpp"
#include "Table5.hpp"
#include "Table7.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table37 final
{
    static constexpr std::string_view TableName = "table_37";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<int32_t, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_2" }> col2;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<Member(Table0::id), Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<Member(Table20::id), Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table18::id), Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }> fk25;
    Light::BelongsTo<Member(Table23::id), Light::SqlRealName { "fk_23" }> fk23;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<Member(Table32::id), Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<Member(Table4::id), Light::SqlRealName { "fk_4" }> fk4;
};

