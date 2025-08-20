// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table10.hpp"
#include "Table11.hpp"
#include "Table12.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table17.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table21.hpp"
#include "Table22.hpp"
#include "Table24.hpp"
#include "Table25.hpp"
#include "Table27.hpp"
#include "Table28.hpp"
#include "Table3.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table35.hpp"
#include "Table4.hpp"
#include "Table5.hpp"
#include "Table6.hpp"
#include "Table7.hpp"
#include "Table8.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table36 final
{
    static constexpr std::string_view TableName = "table_36";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<double, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<double, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<double, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<double, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<double, Light::SqlRealName { "col_28" }> col28;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<int32_t, Light::SqlRealName { "col_31" }> col31;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<bool, Light::SqlRealName { "col_36" }> col36;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_40" }> col40;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }> fk19;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }> fk7;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<&Table33::id, Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
    Light::BelongsTo<&Table25::id, Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }, Light::SqlNullable::Null> fk28;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }> fk6;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }> fk20;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
};

