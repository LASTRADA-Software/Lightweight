// File is automatically generated using ddl2cpp.
#pragma once

#include "Table16.hpp"
#include "Table20.hpp"
#include "Table22.hpp"
#include "Table24.hpp"
#include "Table25.hpp"
#include "Table27.hpp"
#include "Table28.hpp"
#include "Table3.hpp"
#include "Table32.hpp"
#include "Table33.hpp"
#include "Table38.hpp"
#include "Table4.hpp"
#include "Table42.hpp"
#include "Table43.hpp"
#include "Table46.hpp"
#include "Table47.hpp"
#include "Table48.hpp"
#include "Table5.hpp"
#include "Table52.hpp"
#include "Table54.hpp"
#include "Table57.hpp"
#include "Table59.hpp"
#include "Table63.hpp"
#include "Table69.hpp"
#include "Table71.hpp"
#include "Table74.hpp"
#include "Table78.hpp"
#include "Table9.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table80 final
{
    static constexpr std::string_view TableName = "table_80";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_0" }> col0;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<double, Light::SqlRealName { "col_2" }> col2;
    Light::BelongsTo<Member(Table4::id), Light::SqlRealName { "fk_4" }> fk4;
    Light::BelongsTo<Member(Table42::id), Light::SqlRealName { "fk_42" }, Light::SqlNullable::Null> fk42;
    Light::BelongsTo<Member(Table38::id), Light::SqlRealName { "fk_38" }, Light::SqlNullable::Null> fk38;
    Light::BelongsTo<Member(Table47::id), Light::SqlRealName { "fk_47" }, Light::SqlNullable::Null> fk47;
    Light::BelongsTo<Member(Table16::id), Light::SqlRealName { "fk_16" }, Light::SqlNullable::Null> fk16;
    Light::BelongsTo<Member(Table63::id), Light::SqlRealName { "fk_63" }, Light::SqlNullable::Null> fk63;
    Light::BelongsTo<Member(Table69::id), Light::SqlRealName { "fk_69" }> fk69;
    Light::BelongsTo<Member(Table5::id), Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<Member(Table71::id), Light::SqlRealName { "fk_71" }, Light::SqlNullable::Null> fk71;
    Light::BelongsTo<Member(Table43::id), Light::SqlRealName { "fk_43" }, Light::SqlNullable::Null> fk43;
    Light::BelongsTo<Member(Table3::id), Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<Member(Table32::id), Light::SqlRealName { "fk_32" }, Light::SqlNullable::Null> fk32;
    Light::BelongsTo<Member(Table9::id), Light::SqlRealName { "fk_9" }, Light::SqlNullable::Null> fk9;
    Light::BelongsTo<Member(Table24::id), Light::SqlRealName { "fk_24" }> fk24;
    Light::BelongsTo<Member(Table74::id), Light::SqlRealName { "fk_74" }> fk74;
    Light::BelongsTo<Member(Table33::id), Light::SqlRealName { "fk_33" }> fk33;
    Light::BelongsTo<Member(Table78::id), Light::SqlRealName { "fk_78" }> fk78;
    Light::BelongsTo<Member(Table52::id), Light::SqlRealName { "fk_52" }, Light::SqlNullable::Null> fk52;
    Light::BelongsTo<Member(Table57::id), Light::SqlRealName { "fk_57" }, Light::SqlNullable::Null> fk57;
    Light::BelongsTo<Member(Table28::id), Light::SqlRealName { "fk_28" }, Light::SqlNullable::Null> fk28;
    Light::BelongsTo<Member(Table46::id), Light::SqlRealName { "fk_46" }> fk46;
    Light::BelongsTo<Member(Table27::id), Light::SqlRealName { "fk_27" }> fk27;
    Light::BelongsTo<Member(Table59::id), Light::SqlRealName { "fk_59" }> fk59;
    Light::BelongsTo<Member(Table48::id), Light::SqlRealName { "fk_48" }, Light::SqlNullable::Null> fk48;
    Light::BelongsTo<Member(Table54::id), Light::SqlRealName { "fk_54" }, Light::SqlNullable::Null> fk54;
    Light::BelongsTo<Member(Table20::id), Light::SqlRealName { "fk_20" }, Light::SqlNullable::Null> fk20;
    Light::BelongsTo<Member(Table22::id), Light::SqlRealName { "fk_22" }> fk22;
    Light::BelongsTo<Member(Table25::id), Light::SqlRealName { "fk_25" }, Light::SqlNullable::Null> fk25;
};

