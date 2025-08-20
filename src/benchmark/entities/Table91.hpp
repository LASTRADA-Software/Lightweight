// File is automatically generated using ddl2cpp.
#pragma once

#include "Table12.hpp"
#include "Table14.hpp"
#include "Table17.hpp"
#include "Table21.hpp"
#include "Table27.hpp"
#include "Table3.hpp"
#include "Table30.hpp"
#include "Table38.hpp"
#include "Table4.hpp"
#include "Table45.hpp"
#include "Table46.hpp"
#include "Table51.hpp"
#include "Table52.hpp"
#include "Table55.hpp"
#include "Table58.hpp"
#include "Table62.hpp"
#include "Table63.hpp"
#include "Table68.hpp"
#include "Table7.hpp"
#include "Table70.hpp"
#include "Table72.hpp"
#include "Table74.hpp"
#include "Table76.hpp"
#include "Table77.hpp"
#include "Table78.hpp"
#include "Table8.hpp"
#include "Table85.hpp"
#include "Table87.hpp"
#include "Table90.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table91 final
{
    static constexpr std::string_view TableName = "table_91";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<int32_t, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_2" }> col2;
    Light::Field<std::optional<Light::SqlDate>, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_5" }> col5;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_7" }> col7;
    Light::BelongsTo<&Table78::id, Light::SqlRealName { "fk_78" }, Light::SqlNullable::Null> fk78;
    Light::BelongsTo<&Table45::id, Light::SqlRealName { "fk_45" }, Light::SqlNullable::Null> fk45;
    Light::BelongsTo<&Table58::id, Light::SqlRealName { "fk_58" }> fk58;
    Light::BelongsTo<&Table8::id, Light::SqlRealName { "fk_8" }> fk8;
    Light::BelongsTo<&Table72::id, Light::SqlRealName { "fk_72" }, Light::SqlNullable::Null> fk72;
    Light::BelongsTo<&Table12::id, Light::SqlRealName { "fk_12" }> fk12;
    Light::BelongsTo<&Table52::id, Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<&Table38::id, Light::SqlRealName { "fk_38" }> fk38;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }> fk7;
    Light::BelongsTo<&Table62::id, Light::SqlRealName { "fk_62" }> fk62;
    Light::BelongsTo<&Table70::id, Light::SqlRealName { "fk_70" }, Light::SqlNullable::Null> fk70;
    Light::BelongsTo<&Table14::id, Light::SqlRealName { "fk_14" }> fk14;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<&Table4::id, Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<&Table85::id, Light::SqlRealName { "fk_85" }> fk85;
    Light::BelongsTo<&Table74::id, Light::SqlRealName { "fk_74" }> fk74;
    Light::BelongsTo<&Table77::id, Light::SqlRealName { "fk_77" }, Light::SqlNullable::Null> fk77;
    Light::BelongsTo<&Table17::id, Light::SqlRealName { "fk_17" }, Light::SqlNullable::Null> fk17;
    Light::BelongsTo<&Table21::id, Light::SqlRealName { "fk_21" }, Light::SqlNullable::Null> fk21;
    Light::BelongsTo<&Table27::id, Light::SqlRealName { "fk_27" }, Light::SqlNullable::Null> fk27;
    Light::BelongsTo<&Table90::id, Light::SqlRealName { "fk_90" }, Light::SqlNullable::Null> fk90;
    Light::BelongsTo<&Table87::id, Light::SqlRealName { "fk_87" }, Light::SqlNullable::Null> fk87;
    Light::BelongsTo<&Table55::id, Light::SqlRealName { "fk_55" }> fk55;
    Light::BelongsTo<&Table63::id, Light::SqlRealName { "fk_63" }, Light::SqlNullable::Null> fk63;
    Light::BelongsTo<&Table30::id, Light::SqlRealName { "fk_30" }, Light::SqlNullable::Null> fk30;
    Light::BelongsTo<&Table76::id, Light::SqlRealName { "fk_76" }, Light::SqlNullable::Null> fk76;
    Light::BelongsTo<&Table68::id, Light::SqlRealName { "fk_68" }> fk68;
};

