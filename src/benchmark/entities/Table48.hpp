// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table16.hpp"
#include "Table17.hpp"
#include "Table20.hpp"
#include "Table26.hpp"
#include "Table27.hpp"
#include "Table30.hpp"
#include "Table32.hpp"
#include "Table36.hpp"
#include "Table38.hpp"
#include "Table41.hpp"
#include "Table46.hpp"
#include "Table5.hpp"
#include "Table6.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table48 final
{
    static constexpr std::string_view TableName = "table_48";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<double, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<double, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<double, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<bool, Light::SqlRealName { "col_15" }> col15;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_16" }> col16;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<&Table26::id, Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }> fk36;
};

