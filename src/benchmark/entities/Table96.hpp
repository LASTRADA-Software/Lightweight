// File is automatically generated using ddl2cpp.
#pragma once

#include "Table12.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table28.hpp"
#include "Table30.hpp"
#include "Table36.hpp"
#include "Table37.hpp"
#include "Table40.hpp"
#include "Table43.hpp"
#include "Table46.hpp"
#include "Table49.hpp"
#include "Table60.hpp"
#include "Table65.hpp"
#include "Table7.hpp"
#include "Table71.hpp"
#include "Table72.hpp"
#include "Table74.hpp"
#include "Table76.hpp"
#include "Table77.hpp"
#include "Table80.hpp"
#include "Table81.hpp"
#include "Table85.hpp"
#include "Table87.hpp"
#include "Table90.hpp"
#include "Table94.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table96 final
{
    static constexpr std::string_view TableName = "table_96";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<double, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<bool, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<double, Light::SqlRealName { "col_10" }> col10;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<&Table77::id, Light::SqlRealName { "fk_77" }, Light::SqlNullable::Null> fk77;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<&Table87::id, Light::SqlRealName { "fk_87" }> fk87;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<&Table80::id, Light::SqlRealName { "fk_80" }> fk80;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }> fk71;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }, Light::SqlNullable::Null> fk12;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<&Table65::id, Light::SqlRealName { "fk_65" }, Light::SqlNullable::Null> fk65;
    Light::BelongsTo<&Table94::id, Light::SqlRealName { "fk_94" }> fk94;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<&Table60::id, Light::SqlRealName { "fk_60" }> fk60;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<&Table76::id, Light::SqlRealName { "fk_76" }, Light::SqlNullable::Null> fk76;
    Light::BelongsTo<&Table85::id, Light::SqlRealName { "fk_85" }> fk85;
    Light::BelongsTo<&Table81::id, Light::SqlRealName { "fk_81" }, Light::SqlNullable::Null> fk81;
    Light::BelongsTo<&Table72::id, Light::SqlRealName { "fk_72" }> fk72;
    Light::BelongsTo<&Table90::id, Light::SqlRealName { "fk_90" }> fk90;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<&Table74::id, Light::SqlRealName { "fk_74" }, Light::SqlNullable::Null> fk74;
};

