// File is automatically generated using ddl2cpp.
#pragma once

#include "Table17.hpp"
#include "Table20.hpp"
#include "Table24.hpp"
#include "Table29.hpp"
#include "Table30.hpp"
#include "Table31.hpp"
#include "Table35.hpp"
#include "Table5.hpp"
#include "Table50.hpp"
#include "Table53.hpp"
#include "Table55.hpp"
#include "Table58.hpp"
#include "Table64.hpp"
#include "Table75.hpp"
#include "Table76.hpp"
#include "Table8.hpp"
#include "Table86.hpp"
#include "Table90.hpp"
#include "Table92.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table95 final
{
    static constexpr std::string_view TableName = "table_95";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<std::optional<double>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_6" }> col6;
    Light::BelongsTo<Member(Table92::id), Light::SqlRealName { "fk_92" }> fk92;
    Light::BelongsTo<Member(Table90::id), Light::SqlRealName { "fk_90" }, Light::SqlNullable::Null> fk90;
    Light::BelongsTo<Member(Table17::id), Light::SqlRealName { "fk_17" }> fk17;
    Light::BelongsTo<Member(Table55::id), Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<Member(Table29::id), Light::SqlRealName { "fk_29" }, Light::SqlNullable::Null> fk29;
    Light::BelongsTo<Member(Table76::id), Light::SqlRealName { "fk_76" }, Light::SqlNullable::Null> fk76;
    Light::BelongsTo<Member(Table86::id), Light::SqlRealName { "fk_86" }> fk86;
    Light::BelongsTo<Member(Table35::id), Light::SqlRealName { "fk_35" }, Light::SqlNullable::Null> fk35;
    Light::BelongsTo<Member(Table50::id), Light::SqlRealName { "fk_50" }, Light::SqlNullable::Null> fk50;
    Light::BelongsTo<Member(Table64::id), Light::SqlRealName { "fk_64" }> fk64;
    Light::BelongsTo<Member(Table20::id), Light::SqlRealName { "fk_20" }> fk20;
    Light::BelongsTo<Member(Table30::id), Light::SqlRealName { "fk_30" }> fk30;
    Light::BelongsTo<Member(Table31::id), Light::SqlRealName { "fk_31" }> fk31;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<Member(Table58::id), Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<Member(Table53::id), Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<Member(Table24::id), Light::SqlRealName { "fk_24" }> fk24;
    Light::BelongsTo<Member(Table8::id), Light::SqlRealName { "fk_8" }, Light::SqlNullable::Null> fk8;
    Light::BelongsTo<Member(Table75::id), Light::SqlRealName { "fk_75" }, Light::SqlNullable::Null> fk75;
};

