// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table2.hpp"
#include "Table26.hpp"
#include "Table30.hpp"
#include "Table35.hpp"
#include "Table36.hpp"
#include "Table38.hpp"
#include "Table4.hpp"
#include "Table41.hpp"
#include "Table47.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table56.hpp"
#include "Table59.hpp"
#include "Table60.hpp"
#include "Table63.hpp"
#include "Table67.hpp"
#include "Table70.hpp"
#include "Table76.hpp"
#include "Table78.hpp"
#include "Table80.hpp"
#include "Table81.hpp"
#include "Table82.hpp"
#include "Table84.hpp"
#include "Table90.hpp"
#include "Table91.hpp"
#include "Table94.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table95 final
{
    static constexpr std::string_view TableName = "table_95";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<double, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_20" }> col20;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_21" }> col21;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_22" }> col22;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_26" }> col26;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table78::id, Light::SqlRealName { "fk_78" }> fk78;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }> fk38;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }> fk47;
    Light::BelongsTo<&Table60::id, Light::SqlRealName { "fk_60" }, Light::SqlNullable::Null> fk60;
    Light::BelongsTo<&Table26::id, Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
    Light::BelongsTo<&Table84::id, Light::SqlRealName { "fk_84" }> fk84;
    Light::BelongsTo<&Table59::id, Light::SqlRealName { "fk_59" }, Light::SqlNullable::Null> fk59;
    Light::BelongsTo<&Table81::id, Light::SqlRealName { "fk_81" }> fk81;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table94::id, Light::SqlRealName { "fk_94" }> fk94;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<&Table70::id, Light::SqlRealName { "fk_70" }> fk70;
    Light::BelongsTo<&Table76::id, Light::SqlRealName { "fk_76" }> fk76;
    Light::BelongsTo<&Table67::id, Light::SqlRealName { "fk_67" }, Light::SqlNullable::Null> fk67;
    Light::BelongsTo<&Table80::id, Light::SqlRealName { "fk_80" }, Light::SqlNullable::Null> fk80;
    Light::BelongsTo<&Table90::id, Light::SqlRealName { "fk_90" }> fk90;
    Light::BelongsTo<&Table82::id, Light::SqlRealName { "fk_82" }> fk82;
    Light::BelongsTo<&Table63::id, Light::SqlRealName { "fk_63" }, Light::SqlNullable::Null> fk63;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<&Table91::id, Light::SqlRealName { "fk_91" }, Light::SqlNullable::Null> fk91;
};

