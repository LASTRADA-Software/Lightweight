// File is automatically generated using ddl2cpp.
#pragma once

#include "Table17.hpp"
#include "Table25.hpp"
#include "Table28.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table39.hpp"
#include "Table41.hpp"
#include "Table43.hpp"
#include "Table44.hpp"
#include "Table45.hpp"
#include "Table48.hpp"
#include "Table49.hpp"
#include "Table50.hpp"
#include "Table53.hpp"
#include "Table54.hpp"
#include "Table58.hpp"
#include "Table61.hpp"
#include "Table63.hpp"
#include "Table70.hpp"
#include "Table71.hpp"
#include "Table72.hpp"
#include "Table74.hpp"
#include "Table81.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table82 final
{
    static constexpr std::string_view TableName = "table_82";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<int32_t, Light::SqlRealName { "col_4" }> col4;
    Light::Field<double, Light::SqlRealName { "col_5" }> col5;
    Light::Field<double, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<bool, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<double, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<int32_t, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<int32_t, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<int32_t, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_36" }> col36;
    Light::BelongsTo<Member(Table58::id), Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<Member(Table28::id), Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<Member(Table45::id), Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<Member(Table48::id), Light::SqlRealName { "fk_48" }> fk48;
    Light::BelongsTo<Member(Table17::id), Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<Member(Table63::id), Light::SqlRealName { "fk_63" }> fk63;
    Light::BelongsTo<Member(Table74::id), Light::SqlRealName { "fk_74" }> fk74;
    Light::BelongsTo<Member(Table71::id), Light::SqlRealName { "fk_71" }> fk71;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<Member(Table49::id), Light::SqlRealName { "fk_49" }, Light::SqlNullable::Null> fk49;
    Light::BelongsTo<Member(Table43::id), Light::SqlRealName { "fk_43" }> fk43;
    Light::BelongsTo<Member(Table72::id), Light::SqlRealName { "fk_72" }, Light::SqlNullable::Null> fk72;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<Member(Table70::id), Light::SqlRealName { "fk_70" }> fk70;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }> fk54;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }, Light::SqlNullable::Null> fk44;
    Light::BelongsTo<Member(Table50::id), Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }> fk25;
    Light::BelongsTo<Member(Table34::id), Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<Member(Table81::id), Light::SqlRealName { "fk_81" }, Light::SqlNullable::Null> fk81;
    Light::BelongsTo<Member(Table61::id), Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
};

