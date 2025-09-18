// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table10.hpp"
#include "Table14.hpp"
#include "Table16.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table21.hpp"
#include "Table26.hpp"
#include "Table28.hpp"
#include "Table31.hpp"
#include "Table36.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table55.hpp"
#include "Table58.hpp"
#include "Table62.hpp"
#include "Table64.hpp"
#include "Table68.hpp"
#include "Table7.hpp"
#include "Table70.hpp"
#include "Table76.hpp"
#include "Table77.hpp"
#include "Table80.hpp"
#include "Table81.hpp"
#include "Table83.hpp"
#include "Table86.hpp"
#include "Table88.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table90 final
{
    static constexpr std::string_view TableName = "table_90";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<int32_t, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<double, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<int32_t, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<double, Light::SqlRealName { "col_22" }> col22;
    Light::Field<double, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_26" }> col26;
    Light::Field<bool, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<bool, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<double, Light::SqlRealName { "col_33" }> col33;
    Light::Field<double, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_42" }> col42;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_43" }> col43;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_44" }> col44;
    Light::BelongsTo<Member(Table62::id), Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<Member(Table26::id), Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<Member(Table86::id), Light::SqlRealName { "fk_86" }> fk86;
    Light::BelongsTo<Member(Table21::id), Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
    Light::BelongsTo<Member(Table55::id), Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<Member(Table50::id), Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<Member(Table68::id), Light::SqlRealName { "fk_68" }> fk68;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
    Light::BelongsTo<Member(Table76::id), Light::SqlRealName { "fk_76" }, Light::SqlNullable::Null> fk76;
    Light::BelongsTo<Member(Table0::id), Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<Member(Table81::id), Light::SqlRealName { "fk_81" }, Light::SqlNullable::Null> fk81;
    Light::BelongsTo<Member(Table80::id), Light::SqlRealName { "fk_80" }> fk80;
    Light::BelongsTo<Member(Table83::id), Light::SqlRealName { "fk_83" }> fk83;
    Light::BelongsTo<Member(Table58::id), Light::SqlRealName { "fk_58" }> fk58;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<Member(Table28::id), Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<Member(Table77::id), Light::SqlRealName { "fk_77" }> fk77;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<Member(Table20::id), Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<Member(Table64::id), Light::SqlRealName { "fk_64" }> fk64;
    Light::BelongsTo<Member(Table70::id), Light::SqlRealName { "fk_70" }, Light::SqlNullable::Null> fk70;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<Member(Table88::id), Light::SqlRealName { "fk_88" }> fk88;
};

