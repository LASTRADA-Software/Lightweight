// File is automatically generated using ddl2cpp.
#pragma once

#include "Table17.hpp"
#include "Table2.hpp"
#include "Table21.hpp"
#include "Table29.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table36.hpp"
#include "Table44.hpp"
#include "Table46.hpp"
#include "Table48.hpp"
#include "Table49.hpp"
#include "Table55.hpp"
#include "Table58.hpp"
#include "Table60.hpp"
#include "Table62.hpp"
#include "Table68.hpp"
#include "Table69.hpp"
#include "Table73.hpp"
#include "Table74.hpp"
#include "Table78.hpp"
#include "Table8.hpp"
#include "Table84.hpp"
#include "Table85.hpp"
#include "Table86.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table88 final
{
    static constexpr std::string_view TableName = "table_88";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<double, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<int32_t, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<int32_t, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<double, Light::SqlRealName { "col_24" }> col24;
    Light::Field<double, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<bool, Light::SqlRealName { "col_28" }> col28;
    Light::Field<bool, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<int32_t, Light::SqlRealName { "col_33" }> col33;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<double, Light::SqlRealName { "col_37" }> col37;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_38" }> col38;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_42" }> col42;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_43" }> col43;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_44" }> col44;
    Light::BelongsTo<&Table73::id, Light::SqlRealName { "fk_73" }> fk73;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<&Table84::id, Light::SqlRealName { "fk_84" }, Light::SqlNullable::Null> fk84;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }, Light::SqlNullable::Null> fk55;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<&Table74::id, Light::SqlRealName { "fk_74" }> fk74;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<&Table68::id, Light::SqlRealName { "fk_68" }> fk68;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }> fk62;
    Light::BelongsTo<&Table60::id, Light::SqlRealName { "fk_60" }, Light::SqlNullable::Null> fk60;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<&Table86::id, Light::SqlRealName { "fk_86" }, Light::SqlNullable::Null> fk86;
    Light::BelongsTo<&Table78::id, Light::SqlRealName { "fk_78" }> fk78;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<&Table69::id, Light::SqlRealName { "fk_69" }> fk69;
    Light::BelongsTo<&Table85::id, Light::SqlRealName { "fk_85" }> fk85;
    Light::BelongsTo<&Table58::id, Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }, Light::SqlNullable::Null> fk44;
    Light::BelongsTo<&Table33::id, Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
};

