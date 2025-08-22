// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table11.hpp"
#include "Table17.hpp"
#include "Table18.hpp"
#include "Table23.hpp"
#include "Table28.hpp"
#include "Table35.hpp"
#include "Table41.hpp"
#include "Table46.hpp"
#include "Table5.hpp"
#include "Table51.hpp"
#include "Table54.hpp"
#include "Table66.hpp"
#include "Table70.hpp"
#include "Table75.hpp"
#include "Table77.hpp"
#include "Table78.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table80 final
{
    static constexpr std::string_view TableName = "table_80";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_13" }> col13;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }> fk51;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }> fk28;
    Light::BelongsTo<&Table75::id, Light::SqlRealName { "fk_75" }> fk75;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<&Table66::id, Light::SqlRealName { "fk_66" }, Light::SqlNullable::Null> fk66;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<&Table11::id, Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<&Table70::id, Light::SqlRealName { "fk_70" }> fk70;
    Light::BelongsTo<&Table78::id, Light::SqlRealName { "fk_78" }, Light::SqlNullable::Null> fk78;
    Light::BelongsTo<&Table54::id, Light::SqlRealName { "fk_54" }> fk54;
    Light::BelongsTo<&Table77::id, Light::SqlRealName { "fk_77" }> fk77;
};

