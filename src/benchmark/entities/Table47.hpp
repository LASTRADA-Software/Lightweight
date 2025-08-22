// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table10.hpp"
#include "Table12.hpp"
#include "Table14.hpp"
#include "Table17.hpp"
#include "Table19.hpp"
#include "Table23.hpp"
#include "Table26.hpp"
#include "Table27.hpp"
#include "Table28.hpp"
#include "Table29.hpp"
#include "Table3.hpp"
#include "Table35.hpp"
#include "Table38.hpp"
#include "Table39.hpp"
#include "Table40.hpp"
#include "Table45.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table47 final
{
    static constexpr std::string_view TableName = "table_47";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<bool, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<int32_t, Light::SqlRealName { "col_5" }> col5;
    Light::Field<bool, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<bool, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<int32_t, Light::SqlRealName { "col_15" }> col15;
    Light::Field<double, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_17" }> col17;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }> fk40;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }, Light::SqlNullable::Null> fk28;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<&Table26::id, Light::SqlRealName { "fk_26" }, Light::SqlNullable::Null> fk26;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }, Light::SqlNullable::Null> fk39;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
};

