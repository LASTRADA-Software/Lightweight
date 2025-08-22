// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table18.hpp"
#include "Table19.hpp"
#include "Table20.hpp"
#include "Table39.hpp"
#include "Table41.hpp"
#include "Table44.hpp"
#include "Table47.hpp"
#include "Table50.hpp"
#include "Table51.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table55.hpp"
#include "Table56.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table58 final
{
    static constexpr std::string_view TableName = "table_58";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_8" }> col8;
    Light::Field<double, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<bool, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_13" }> col13;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }> fk19;
    Light::BelongsTo<&Table53::id, Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }, Light::SqlNullable::Null> fk15;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }> fk47;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }, Light::SqlNullable::Null> fk44;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }> fk20;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }, Light::SqlNullable::Null> fk39;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }> fk50;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table18::id, Light::SqlRealName { "fk_18" }, Light::SqlNullable::Null> fk18;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }, Light::SqlNullable::Null> fk1;
};

