// File is automatically generated using ddl2cpp.
#pragma once

#include "Table15.hpp"
#include "Table2.hpp"
#include "Table40.hpp"
#include "Table41.hpp"
#include "Table47.hpp"
#include "Table59.hpp"
#include "Table62.hpp"
#include "Table64.hpp"
#include "Table68.hpp"
#include "Table69.hpp"
#include "Table71.hpp"
#include "Table73.hpp"
#include "Table74.hpp"
#include "Table76.hpp"
#include "Table78.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table82 final
{
    static constexpr std::string_view TableName = "table_82";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<bool, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_5" }> col5;
    Light::Field<double, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_11" }> col11;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }> fk71;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<&Table59::id, Light::SqlRealName { "fk_59" }> fk59;
    Light::BelongsTo<&Table64::id, Light::SqlRealName { "fk_64" }> fk64;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<&Table78::id, Light::SqlRealName { "fk_78" }, Light::SqlNullable::Null> fk78;
    Light::BelongsTo<&Table68::id, Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
    Light::BelongsTo<&Table74::id, Light::SqlRealName { "fk_74" }, Light::SqlNullable::Null> fk74;
    Light::BelongsTo<&Table73::id, Light::SqlRealName { "fk_73" }, Light::SqlNullable::Null> fk73;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }> fk40;
    Light::BelongsTo<&Table69::id, Light::SqlRealName { "fk_69" }> fk69;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table76::id, Light::SqlRealName { "fk_76" }, Light::SqlNullable::Null> fk76;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
};

