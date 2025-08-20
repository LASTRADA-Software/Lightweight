// File is automatically generated using ddl2cpp.
#pragma once

#include "Table14.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table17.hpp"
#include "Table18.hpp"
#include "Table2.hpp"
#include "Table25.hpp"
#include "Table27.hpp"
#include "Table3.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table33.hpp"
#include "Table35.hpp"
#include "Table37.hpp"
#include "Table42.hpp"
#include "Table46.hpp"
#include "Table49.hpp"
#include "Table5.hpp"
#include "Table52.hpp"
#include "Table59.hpp"
#include "Table61.hpp"
#include "Table66.hpp"
#include "Table68.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table69 final
{
    static constexpr std::string_view TableName = "table_69";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<double, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<bool, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<double, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<double, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<int32_t, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<double, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_40" }> col40;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<bool, Light::SqlRealName { "col_42" }> col42;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_43" }> col43;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_44" }> col44;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_45" }> col45;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
    Light::BelongsTo<Member(Table46::id), Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<Member(Table49::id), Light::SqlRealName { "fk_49" }, Light::SqlNullable::Null> fk49;
    Light::BelongsTo<Member(Table18::id), Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<Member(Table66::id), Light::SqlRealName { "fk_66" }, Light::SqlNullable::Null> fk66;
    Light::BelongsTo<Member(Table59::id), Light::SqlRealName { "fk_59" }> fk59;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<Member(Table17::id), Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<Member(Table37::id), Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<Member(Table61::id), Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<Member(Table30::id), Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<Member(Table42::id), Light::SqlRealName { "fk_42" }> fk42;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }> fk33;
    Light::BelongsTo<Member(Table52::id), Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<Member(Table68::id), Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }> fk27;
};

