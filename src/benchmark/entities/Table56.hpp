// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table12.hpp"
#include "Table13.hpp"
#include "Table14.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table20.hpp"
#include "Table23.hpp"
#include "Table24.hpp"
#include "Table25.hpp"
#include "Table27.hpp"
#include "Table28.hpp"
#include "Table3.hpp"
#include "Table34.hpp"
#include "Table37.hpp"
#include "Table41.hpp"
#include "Table45.hpp"
#include "Table51.hpp"
#include "Table52.hpp"
#include "Table53.hpp"
#include "Table54.hpp"
#include "Table6.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table56 final
{
    static constexpr std::string_view TableName = "table_56";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<bool, Light::SqlRealName { "col_0" }> col0;
    Light::Field<double, Light::SqlRealName { "col_1" }> col1;
    Light::Field<double, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_4" }> col4;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }> fk45;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<&Table41::id, Light::SqlRealName { "fk_41" }> fk41;
    Light::BelongsTo<&Table20::id, Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<&Table37::id, Light::SqlRealName { "fk_37" }> fk37;
    Light::BelongsTo<&Table28::id, Light::SqlRealName { "fk_28" }, Light::SqlNullable::Null> fk28;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }> fk19;
    Light::BelongsTo<&Table25::id, Light::SqlRealName { "fk_25" }> fk25;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }> fk6;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }> fk0;
    Light::BelongsTo<&Table53::id, Light::SqlRealName { "fk_53" }, Light::SqlNullable::Null> fk53;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
    Light::BelongsTo<&Table24::id, Light::SqlRealName { "fk_24" }, Light::SqlNullable::Null> fk24;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table34::id, Light::SqlRealName { "fk_34" }> fk34;
    Light::BelongsTo<&Table54::id, Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<&Table13::id, Light::SqlRealName { "fk_13" }, Light::SqlNullable::Null> fk13;
    Light::BelongsTo<&Table23::id, Light::SqlRealName { "fk_23" }> fk23;
};

