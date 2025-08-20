// File is automatically generated using ddl2cpp.
#pragma once

#include "Table18.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table25.hpp"
#include "Table26.hpp"
#include "Table3.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table34.hpp"
#include "Table38.hpp"
#include "Table43.hpp"
#include "Table46.hpp"
#include "Table48.hpp"
#include "Table49.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table54.hpp"
#include "Table55.hpp"
#include "Table6.hpp"
#include "Table61.hpp"
#include "Table62.hpp"
#include "Table64.hpp"
#include "Table69.hpp"
#include "Table70.hpp"
#include "Table71.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table73 final
{
    static constexpr std::string_view TableName = "table_73";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<double, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_10" }> col10;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<&Table64::id, Light::SqlRealName { "fk_64" }, Light::SqlNullable::Null> fk64;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }, Light::SqlNullable::Null> fk55;
    Light::BelongsTo<&Table25::id, Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }> fk38;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }> fk48;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<&Table54::id, Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<&Table53::id, Light::SqlRealName { "fk_53" }> fk53;
    Light::BelongsTo<&Table31::id, Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<&Table69::id, Light::SqlRealName { "fk_69" }> fk69;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }> fk71;
    Light::BelongsTo<&Table61::id, Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }, Light::SqlNullable::Null> fk62;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<&Table70::id, Light::SqlRealName { "fk_70" }, Light::SqlNullable::Null> fk70;
    Light::BelongsTo<&Table26::id, Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
};

