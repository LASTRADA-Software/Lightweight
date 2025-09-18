// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table17.hpp"
#include "Table20.hpp"
#include "Table22.hpp"
#include "Table25.hpp"
#include "Table29.hpp"
#include "Table32.hpp"
#include "Table38.hpp"
#include "Table39.hpp"
#include "Table4.hpp"
#include "Table43.hpp"
#include "Table44.hpp"
#include "Table47.hpp"
#include "Table48.hpp"
#include "Table5.hpp"
#include "Table51.hpp"
#include "Table53.hpp"
#include "Table58.hpp"
#include "Table59.hpp"
#include "Table61.hpp"
#include "Table64.hpp"
#include "Table65.hpp"
#include "Table69.hpp"
#include "Table71.hpp"
#include "Table73.hpp"
#include "Table75.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table76 final
{
    static constexpr std::string_view TableName = "table_76";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<int32_t, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<double, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<double, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<int32_t, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<double, Light::SqlRealName { "col_33" }> col33;
    Light::Field<double, Light::SqlRealName { "col_34" }> col34;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<int32_t, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_41" }> col41;
    Light::BelongsTo<Member(Table4::id), Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<Member(Table69::id), Light::SqlRealName { "fk_69" }, Light::SqlNullable::Null> fk69;
    Light::BelongsTo<Member(Table75::id), Light::SqlRealName { "fk_75" }> fk75;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<Member(Table32::id), Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<Member(Table71::id), Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<Member(Table64::id), Light::SqlRealName { "fk_64" }> fk64;
    Light::BelongsTo<Member(Table58::id), Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }, Light::SqlNullable::Null> fk39;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<Member(Table61::id), Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<Member(Table59::id), Light::SqlRealName { "fk_59" }> fk59;
    Light::BelongsTo<Member(Table0::id), Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<Member(Table51::id), Light::SqlRealName { "fk_51" }> fk51;
    Light::BelongsTo<Member(Table43::id), Light::SqlRealName { "fk_43" }> fk43;
    Light::BelongsTo<Member(Table65::id), Light::SqlRealName { "fk_65" }> fk65;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }> fk25;
    Light::BelongsTo<Member(Table20::id), Light::SqlRealName { "fk_20" }> fk20;
    Light::BelongsTo<Member(Table48::id), Light::SqlRealName { "fk_48" }> fk48;
    Light::BelongsTo<Member(Table73::id), Light::SqlRealName { "fk_73" }> fk73;
    Light::BelongsTo<Member(Table17::id), Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<Member(Table38::id), Light::SqlRealName { "fk_38" }> fk38;
};

