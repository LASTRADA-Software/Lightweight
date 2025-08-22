// File is automatically generated using ddl2cpp.
#pragma once

#include "Table15.hpp"
#include "Table17.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table22.hpp"
#include "Table29.hpp"
#include "Table32.hpp"
#include "Table33.hpp"
#include "Table37.hpp"
#include "Table39.hpp"
#include "Table4.hpp"
#include "Table41.hpp"
#include "Table42.hpp"
#include "Table44.hpp"
#include "Table46.hpp"
#include "Table48.hpp"
#include "Table49.hpp"
#include "Table51.hpp"
#include "Table55.hpp"
#include "Table56.hpp"
#include "Table57.hpp"
#include "Table60.hpp"
#include "Table64.hpp"
#include "Table65.hpp"
#include "Table66.hpp"
#include "Table67.hpp"
#include "Table72.hpp"
#include "Table75.hpp"
#include "Table80.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table83 final
{
    static constexpr std::string_view TableName = "table_83";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<bool, Light::SqlRealName { "col_1" }> col1;
    Light::Field<int32_t, Light::SqlRealName { "col_2" }> col2;
    Light::Field<bool, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_7" }> col7;
    Light::Field<double, Light::SqlRealName { "col_8" }> col8;
    Light::Field<double, Light::SqlRealName { "col_9" }> col9;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<&Table33::id, Light::SqlRealName { "fk_33" }> fk33;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }> fk19;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }, Light::SqlNullable::Null> fk44;
    Light::BelongsTo<&Table57::id, Light::SqlRealName { "fk_57" }, Light::SqlNullable::Null> fk57;
    Light::BelongsTo<&Table64::id, Light::SqlRealName { "fk_64" }, Light::SqlNullable::Null> fk64;
    Light::BelongsTo<&Table32::id, Light::SqlRealName { "fk_32" }> fk32;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }, Light::SqlNullable::Null> fk4;
    Light::BelongsTo<&Table75::id, Light::SqlRealName { "fk_75" }> fk75;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<&Table65::id, Light::SqlRealName { "fk_65" }, Light::SqlNullable::Null> fk65;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }, Light::SqlNullable::Null> fk37;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<&Table80::id, Light::SqlRealName { "fk_80" }, Light::SqlNullable::Null> fk80;
    Light::BelongsTo<&Table9::id, Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<&Table60::id, Light::SqlRealName { "fk_60" }> fk60;
    Light::BelongsTo<&Table66::id, Light::SqlRealName { "fk_66" }> fk66;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }> fk48;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }, Light::SqlNullable::Null> fk55;
    Light::BelongsTo<&Table42::id, Light::SqlRealName { "fk_42" }, Light::SqlNullable::Null> fk42;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<&Table56::id, Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<&Table72::id, Light::SqlRealName { "fk_72" }, Light::SqlNullable::Null> fk72;
    Light::BelongsTo<&Table39::id, Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<&Table67::id, Light::SqlRealName { "fk_67" }, Light::SqlNullable::Null> fk67;
    Light::BelongsTo<&Table22::id, Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
};

