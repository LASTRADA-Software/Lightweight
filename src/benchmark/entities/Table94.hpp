// File is automatically generated using ddl2cpp.
#pragma once

#include "Table10.hpp"
#include "Table11.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table27.hpp"
#include "Table29.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table40.hpp"
#include "Table47.hpp"
#include "Table55.hpp"
#include "Table56.hpp"
#include "Table59.hpp"
#include "Table60.hpp"
#include "Table68.hpp"
#include "Table71.hpp"
#include "Table78.hpp"
#include "Table80.hpp"
#include "Table82.hpp"
#include "Table83.hpp"
#include "Table85.hpp"
#include "Table91.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table94 final
{
    static constexpr std::string_view TableName = "table_94";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<double, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<int32_t, Light::SqlRealName { "col_11" }> col11;
    Light::BelongsTo<&Table80::id, Light::SqlRealName { "fk_80" }> fk80;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }> fk11;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<&Table83::id, Light::SqlRealName { "fk_83" }, Light::SqlNullable::Null> fk83;
    Light::BelongsTo<&Table91::id, Light::SqlRealName { "fk_91" }, Light::SqlNullable::Null> fk91;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<&Table82::id, Light::SqlRealName { "fk_82" }, Light::SqlNullable::Null> fk82;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<&Table78::id, Light::SqlRealName { "fk_78" }, Light::SqlNullable::Null> fk78;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table10::id, Light::SqlRealName { "fk_10" }, Light::SqlNullable::Null> fk10;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }> fk40;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table60::id, Light::SqlRealName { "fk_60" }, Light::SqlNullable::Null> fk60;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }> fk47;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }> fk29;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }, Light::SqlNullable::Null> fk14;
    Light::BelongsTo<&Table68::id, Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<&Table59::id, Light::SqlRealName { "fk_59" }, Light::SqlNullable::Null> fk59;
    Light::BelongsTo<&Table85::id, Light::SqlRealName { "fk_85" }> fk85;
};

