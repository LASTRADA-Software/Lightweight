// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table17.hpp"
#include "Table20.hpp"
#include "Table24.hpp"
#include "Table27.hpp"
#include "Table28.hpp"
#include "Table3.hpp"
#include "Table36.hpp"
#include "Table46.hpp"
#include "Table52.hpp"
#include "Table54.hpp"
#include "Table58.hpp"
#include "Table60.hpp"
#include "Table63.hpp"
#include "Table64.hpp"
#include "Table65.hpp"
#include "Table66.hpp"
#include "Table69.hpp"
#include "Table71.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table72 final
{
    static constexpr std::string_view TableName = "table_72";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<bool, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_15" }> col15;
    Light::BelongsTo<&Table60::id, Light::SqlRealName { "fk_60" }, Light::SqlNullable::Null> fk60;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table54::id, Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<&Table58::id, Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<&Table64::id, Light::SqlRealName { "fk_64" }, Light::SqlNullable::Null> fk64;
    Light::BelongsTo<&Table69::id, Light::SqlRealName { "fk_69" }, Light::SqlNullable::Null> fk69;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }> fk71;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<&Table66::id, Light::SqlRealName { "fk_66" }> fk66;
    Light::BelongsTo<&Table65::id, Light::SqlRealName { "fk_65" }> fk65;
    Light::BelongsTo<&Table63::id, Light::SqlRealName { "fk_63" }, Light::SqlNullable::Null> fk63;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }> fk20;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }> fk24;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
};

