// File is automatically generated using ddl2cpp.
#pragma once

#include "Table16.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table22.hpp"
#include "Table25.hpp"
#include "Table26.hpp"
#include "Table3.hpp"
#include "Table30.hpp"
#include "Table35.hpp"
#include "Table37.hpp"
#include "Table41.hpp"
#include "Table44.hpp"
#include "Table45.hpp"
#include "Table47.hpp"
#include "Table49.hpp"
#include "Table50.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table55.hpp"
#include "Table56.hpp"
#include "Table60.hpp"
#include "Table61.hpp"
#include "Table68.hpp"
#include "Table71.hpp"
#include "Table73.hpp"
#include "Table75.hpp"
#include "Table76.hpp"
#include "Table77.hpp"
#include "Table79.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table83 final
{
    static constexpr std::string_view TableName = "table_83";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<int32_t, Light::SqlRealName { "col_1" }> col1;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_2" }> col2;
    Light::BelongsTo<Member(Table44::id), Light::SqlRealName { "fk_44" }> fk44;
    Light::BelongsTo<Member(Table2::id), Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }> fk35;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }> fk47;
    Light::BelongsTo<Member(Table30::id), Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<Member(Table68::id), Light::SqlRealName { "fk_68" }, Light::SqlNullable::Null> fk68;
    Light::BelongsTo<Member(Table76::id), Light::SqlRealName { "fk_76" }> fk76;
    Light::BelongsTo<Member(Table45::id), Light::SqlRealName { "fk_45" }, Light::SqlNullable::Null> fk45;
    Light::BelongsTo<Member(Table75::id), Light::SqlRealName { "fk_75" }> fk75;
    Light::BelongsTo<Member(Table61::id), Light::SqlRealName { "fk_61" }, Light::SqlNullable::Null> fk61;
    Light::BelongsTo<Member(Table20::id), Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<Member(Table79::id), Light::SqlRealName { "fk_79" }, Light::SqlNullable::Null> fk79;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<Member(Table55::id), Light::SqlRealName { "fk_55" }, Light::SqlNullable::Null> fk55;
    Light::BelongsTo<Member(Table52::id), Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<Member(Table37::id), Light::SqlRealName { "fk_37" }> fk37;
    Light::BelongsTo<Member(Table9::id), Light::SqlRealName { "fk_9" }> fk9;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }> fk25;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }, Light::SqlNullable::Null> fk22;
    Light::BelongsTo<Member(Table50::id), Light::SqlRealName { "fk_50" }> fk50;
    Light::BelongsTo<Member(Table71::id), Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<Member(Table56::id), Light::SqlRealName { "fk_56" }> fk56;
    Light::BelongsTo<Member(Table60::id), Light::SqlRealName { "fk_60" }, Light::SqlNullable::Null> fk60;
    Light::BelongsTo<Member(Table26::id), Light::SqlRealName { "fk_26" }, Light::SqlNullable::Null> fk26;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }> fk3;
    Light::BelongsTo<Member(Table77::id), Light::SqlRealName { "fk_77" }> fk77;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<Member(Table73::id), Light::SqlRealName { "fk_73" }, Light::SqlNullable::Null> fk73;
    Light::BelongsTo<Member(Table49::id), Light::SqlRealName { "fk_49" }> fk49;
    Light::BelongsTo<Member(Table41::id), Light::SqlRealName { "fk_41" }, Light::SqlNullable::Null> fk41;
};

