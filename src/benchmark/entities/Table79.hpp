// File is automatically generated using ddl2cpp.
#pragma once

#include "Table17.hpp"
#include "Table18.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table21.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table35.hpp"
#include "Table44.hpp"
#include "Table5.hpp"
#include "Table55.hpp"
#include "Table56.hpp"
#include "Table6.hpp"
#include "Table65.hpp"
#include "Table66.hpp"
#include "Table69.hpp"
#include "Table71.hpp"
#include "Table72.hpp"
#include "Table76.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table79 final
{
    static constexpr std::string_view TableName = "table_79";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_3" }> col3;
    Light::BelongsTo<&Table65::id, Light::SqlRealName { "fk_65" }, Light::SqlNullable::Null> fk65;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }> fk6;
    Light::BelongsTo<&Table69::id, Light::SqlRealName { "fk_69" }, Light::SqlNullable::Null> fk69;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<&Table72::id, Light::SqlRealName { "fk_72" }, Light::SqlNullable::Null> fk72;
    Light::BelongsTo<&Table66::id, Light::SqlRealName { "fk_66" }> fk66;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }, Light::SqlNullable::Null> fk55;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<&Table76::id, Light::SqlRealName { "fk_76" }> fk76;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<&Table33::id, Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }> fk44;
};

