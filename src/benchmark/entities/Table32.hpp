// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table10.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table17.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table23.hpp"
#include "Table27.hpp"
#include "Table28.hpp"
#include "Table29.hpp"
#include "Table3.hpp"
#include "Table4.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table32 final
{
    static constexpr std::string_view TableName = "table_32";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<int32_t, Light::SqlRealName { "col_0" }> col0;
    Light::Field<double, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<double, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<double, Light::SqlRealName { "col_11" }> col11;
    Light::Field<int32_t, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<int32_t, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<bool, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<int32_t, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<double, Light::SqlRealName { "col_40" }> col40;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_42" }> col42;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_43" }> col43;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table23::id), Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
    Light::BelongsTo<Member(Table15::id), Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<Member(Table28::id), Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<Member(Table17::id), Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
    Light::BelongsTo<Member(Table19::id), Light::SqlRealName { "fk_19" }> fk19;
    Light::BelongsTo<Member(Table10::id), Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<Member(Table0::id), Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<Member(Table20::id), Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<Member(Table4::id), Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
};

