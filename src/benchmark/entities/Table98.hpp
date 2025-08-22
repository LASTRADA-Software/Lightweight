// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table18.hpp"
#include "Table2.hpp"
#include "Table21.hpp"
#include "Table24.hpp"
#include "Table27.hpp"
#include "Table29.hpp"
#include "Table3.hpp"
#include "Table31.hpp"
#include "Table37.hpp"
#include "Table4.hpp"
#include "Table40.hpp"
#include "Table43.hpp"
#include "Table51.hpp"
#include "Table56.hpp"
#include "Table57.hpp"
#include "Table60.hpp"
#include "Table61.hpp"
#include "Table73.hpp"
#include "Table83.hpp"
#include "Table84.hpp"
#include "Table86.hpp"
#include "Table90.hpp"
#include "Table92.hpp"
#include "Table93.hpp"
#include "Table96.hpp"
#include "Table97.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table98 final
{
    static constexpr std::string_view TableName = "table_98";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_3" }> col3;
    Light::Field<double, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<double, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_18" }> col18;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_19" }> col19;
    Light::Field<int32_t, Light::SqlRealName { "col_20" }> col20;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_21" }> col21;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_22" }> col22;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_23" }> col23;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_24" }> col24;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_25" }> col25;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_26" }> col26;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_27" }> col27;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_28" }> col28;
    Light::Field<double, Light::SqlRealName { "col_29" }> col29;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_30" }> col30;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_31" }> col31;
    Light::Field<bool, Light::SqlRealName { "col_32" }> col32;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_33" }> col33;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_34" }> col34;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_35" }> col35;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_36" }> col36;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_37" }> col37;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_38" }> col38;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_39" }> col39;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_40" }> col40;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_41" }> col41;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_42" }> col42;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_43" }> col43;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<&Table86::id, Light::SqlRealName { "fk_86" }> fk86;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table93::id, Light::SqlRealName { "fk_93" }, Light::SqlNullable::Null> fk93;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<&Table57::id, Light::SqlRealName { "fk_57" }, Light::SqlNullable::Null> fk57;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table73::id, Light::SqlRealName { "fk_73" }, Light::SqlNullable::Null> fk73;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<&Table61::id, Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table83::id, Light::SqlRealName { "fk_83" }, Light::SqlNullable::Null> fk83;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }> fk24;
    Light::BelongsTo<&Table60::id, Light::SqlRealName { "fk_60" }> fk60;
    Light::BelongsTo<&Table90::id, Light::SqlRealName { "fk_90" }> fk90;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table97::id, Light::SqlRealName { "fk_97" }, Light::SqlNullable::Null> fk97;
    Light::BelongsTo<&Table96::id, Light::SqlRealName { "fk_96" }, Light::SqlNullable::Null> fk96;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table84::id, Light::SqlRealName { "fk_84" }, Light::SqlNullable::Null> fk84;
    Light::BelongsTo<&Table92::id, Light::SqlRealName { "fk_92" }, Light::SqlNullable::Null> fk92;
};

