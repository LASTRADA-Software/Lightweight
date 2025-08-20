// File is automatically generated using ddl2cpp.
#pragma once

#include "Table14.hpp"
#include "Table18.hpp"
#include "Table2.hpp"
#include "Table37.hpp"
#include "Table39.hpp"
#include "Table41.hpp"
#include "Table47.hpp"
#include "Table54.hpp"
#include "Table57.hpp"
#include "Table63.hpp"
#include "Table73.hpp"
#include "Table8.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table81 final
{
    static constexpr std::string_view TableName = "table_81";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_2" }> col2;
    Light::Field<int32_t, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_5" }> col5;
    Light::Field<int32_t, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<int32_t, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_11" }> col11;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_16" }> col16;
    Light::BelongsTo<Member(Table73::id), Light::SqlRealName { "fk_73" }> fk73;
    Light::BelongsTo<Member(Table18::id), Light::SqlRealName { "fk_18" }> fk18;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<Member(Table57::id), Light::SqlRealName { "fk_57" }> fk57;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }, Light::SqlNullable::Null> fk2;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<Member(Table37::id), Light::SqlRealName { "fk_37" }> fk37;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<Member(Table14::id), Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<Member(Table63::id), Light::SqlRealName { "fk_63" }, Light::SqlNullable::Null> fk63;
};

