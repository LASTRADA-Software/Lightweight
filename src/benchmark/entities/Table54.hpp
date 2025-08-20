// File is automatically generated using ddl2cpp.
#pragma once

#include "Table1.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table17.hpp"
#include "Table2.hpp"
#include "Table21.hpp"
#include "Table23.hpp"
#include "Table27.hpp"
#include "Table29.hpp"
#include "Table3.hpp"
#include "Table33.hpp"
#include "Table34.hpp"
#include "Table35.hpp"
#include "Table36.hpp"
#include "Table38.hpp"
#include "Table41.hpp"
#include "Table42.hpp"
#include "Table44.hpp"
#include "Table46.hpp"
#include "Table47.hpp"
#include "Table48.hpp"
#include "Table49.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table51.hpp"
#include "Table52.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table54 final
{
    static constexpr std::string_view TableName = "table_54";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<bool, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_5" }> col5;
    Light::BelongsTo<&Table36::id, Light::SqlRealName { "fk_36" }, Light::SqlNullable::Null> fk36;
    Light::BelongsTo<&Table33::id, Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }, Light::SqlNullable::Null> fk23;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table29::id, Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<&Table35::id, Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<&Table48::id, Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }> fk5;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }> fk1;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<&Table47::id, Light::SqlRealName { "fk_47" }> fk47;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<&Table42::id, Light::SqlRealName { "fk_42" }, Light::SqlNullable::Null> fk42;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }, Light::SqlNullable::Null> fk34;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }> fk21;
    Light::BelongsTo<&Table50::id, Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<&Table49::id, Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<&Table44::id, Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }> fk38;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
};

