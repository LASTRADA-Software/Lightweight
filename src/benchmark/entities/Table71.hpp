// File is automatically generated using ddl2cpp.
#pragma once

#include "Table11.hpp"
#include "Table12.hpp"
#include "Table17.hpp"
#include "Table2.hpp"
#include "Table23.hpp"
#include "Table24.hpp"
#include "Table28.hpp"
#include "Table32.hpp"
#include "Table34.hpp"
#include "Table36.hpp"
#include "Table37.hpp"
#include "Table4.hpp"
#include "Table43.hpp"
#include "Table45.hpp"
#include "Table47.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table51.hpp"
#include "Table53.hpp"
#include "Table54.hpp"
#include "Table56.hpp"
#include "Table61.hpp"
#include "Table64.hpp"
#include "Table67.hpp"
#include "Table68.hpp"
#include "Table69.hpp"
#include "Table7.hpp"
#include "Table70.hpp"
#include "Table8.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table71 final
{
    static constexpr std::string_view TableName = "table_71";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<double, Light::SqlRealName { "col_2" }> col2;
    Light::Field<double, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<bool, Light::SqlRealName { "col_6" }> col6;
    Light::Field<double, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_9" }> col9;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table54::id, Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<&Table53::id, Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }> fk11;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table69::id, Light::SqlRealName { "fk_69" }, Light::SqlNullable::Null> fk69;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<&Table67::id, Light::SqlRealName { "fk_67" }, Light::SqlNullable::Null> fk67;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<&Table68::id, Light::SqlRealName { "fk_68" }> fk68;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }> fk47;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }, Light::SqlNullable::Null> fk28;
    Light::BelongsTo<&Table64::id, Light::SqlRealName { "fk_64" }, Light::SqlNullable::Null> fk64;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }> fk50;
    Light::BelongsTo<&Table61::id, Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table43::id, Light::SqlRealName { "fk_43" }> fk43;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<&Table70::id, Light::SqlRealName { "fk_70" }> fk70;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }> fk7;
};

