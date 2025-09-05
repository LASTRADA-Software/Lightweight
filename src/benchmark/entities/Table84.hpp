// File is automatically generated using ddl2cpp.
#pragma once

#include "Table11.hpp"
#include "Table12.hpp"
#include "Table27.hpp"
#include "Table31.hpp"
#include "Table33.hpp"
#include "Table35.hpp"
#include "Table36.hpp"
#include "Table39.hpp"
#include "Table41.hpp"
#include "Table45.hpp"
#include "Table47.hpp"
#include "Table50.hpp"
#include "Table51.hpp"
#include "Table54.hpp"
#include "Table56.hpp"
#include "Table6.hpp"
#include "Table61.hpp"
#include "Table62.hpp"
#include "Table63.hpp"
#include "Table68.hpp"
#include "Table70.hpp"
#include "Table72.hpp"
#include "Table75.hpp"
#include "Table76.hpp"
#include "Table79.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table84 final
{
    static constexpr std::string_view TableName = "table_84";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<bool>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<int32_t, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_6" }> col6;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_7" }> col7;
    Light::BelongsTo<Member(Table39::id), Light::SqlRealName { "fk_39" }> fk39;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }, Light::SqlNullable::Null> fk31;
    Light::BelongsTo<Member(Table70::id), Light::SqlRealName { "fk_70" }, Light::SqlNullable::Null> fk70;
    Light::BelongsTo<Member(Table76::id), Light::SqlRealName { "fk_76" }, Light::SqlNullable::Null> fk76;
    Light::BelongsTo<Member(Table75::id), Light::SqlRealName { "fk_75" }, Light::SqlNullable::Null> fk75;
    Light::BelongsTo<Member(Table50::id), Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<Member(Table45::id), Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<Member(Table79::id), Light::SqlRealName { "fk_79" }, Light::SqlNullable::Null> fk79;
    Light::BelongsTo<Member(Table6::id), Light::SqlRealName { "fk_6" }> fk6;
    Light::BelongsTo<Member(Table68::id), Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
    Light::BelongsTo<Member(Table12::id), Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<Member(Table11::id), Light::SqlRealName { "fk_11" }, Light::SqlNullable::Null> fk11;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
    Light::BelongsTo<Member(Table72::id), Light::SqlRealName { "fk_72" }, Light::SqlNullable::Null> fk72;
    Light::BelongsTo<Member(Table62::id), Light::SqlRealName { "fk_62" }> fk62;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }, Light::SqlNullable::Null> fk33;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<Member(Table51::id), Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<Member(Table63::id), Light::SqlRealName { "fk_63" }, Light::SqlNullable::Null> fk63;
    Light::BelongsTo<Member(Table56::id), Light::SqlRealName { "fk_56" }, Light::SqlNullable::Null> fk56;
    Light::BelongsTo<Member(Table36::id), Light::SqlRealName { "fk_36" }> fk36;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }> fk47;
    Light::BelongsTo<Member(Table61::id), Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
};

