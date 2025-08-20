// File is automatically generated using ddl2cpp.
#pragma once

#include "Table12.hpp"
#include "Table14.hpp"
#include "Table16.hpp"
#include "Table22.hpp"
#include "Table25.hpp"
#include "Table27.hpp"
#include "Table3.hpp"
#include "Table35.hpp"
#include "Table36.hpp"
#include "Table38.hpp"
#include "Table39.hpp"
#include "Table40.hpp"
#include "Table41.hpp"
#include "Table48.hpp"
#include "Table49.hpp"
#include "Table50.hpp"
#include "Table55.hpp"
#include "Table60.hpp"
#include "Table61.hpp"
#include "Table62.hpp"
#include "Table63.hpp"
#include "Table69.hpp"
#include "Table73.hpp"
#include "Table76.hpp"
#include "Table8.hpp"
#include "Table81.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table86 final
{
    static constexpr std::string_view TableName = "table_86";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<int32_t, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<double, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<double, Light::SqlRealName { "col_24" }> col24;
    Light::Field<int32_t, Light::SqlRealName { "col_25" }> col25;
    Light::Field<bool, Light::SqlRealName { "col_26" }> col26;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_29" }> col29;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_30" }> col30;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_32" }> col32;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_35" }> col35;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<double, Light::SqlRealName { "col_38" }> col38;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_42" }> col42;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_43" }> col43;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_44" }> col44;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }> fk50;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }> fk38;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<&Table69::id, Light::SqlRealName { "fk_69" }> fk69;
    Light::BelongsTo<&Table63::id, Light::SqlRealName { "fk_63" }, Light::SqlNullable::Null> fk63;
    Light::BelongsTo<&Table25::id, Light::SqlRealName { "fk_25" }> fk25;
    Light::BelongsTo<&Table76::id, Light::SqlRealName { "fk_76" }, Light::SqlNullable::Null> fk76;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<&Table81::id, Light::SqlRealName { "fk_81" }> fk81;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<&Table73::id, Light::SqlRealName { "fk_73" }> fk73;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<&Table61::id, Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }> fk62;
    Light::BelongsTo<&Table60::id, Light::SqlRealName { "fk_60" }> fk60;
};

