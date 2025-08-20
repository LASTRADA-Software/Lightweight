// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table2.hpp"
#include "Table3.hpp"
#include "Table4.hpp"
#include "Table5.hpp"
#include "Table6.hpp"
#include "Table7.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table8 final
{
    static constexpr std::string_view TableName = "table_8";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<bool, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_8" }> col8;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_14" }> col14;
    Light::BelongsTo<Member(Table7::id), Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<Member(Table4::id), Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<Member(Table0::id), Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<Member(Table6::id), Light::SqlRealName { "fk_6" }, Light::SqlNullable::Null> fk6;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<Member(Table1::id), Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
};

