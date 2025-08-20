// File is automatically generated using ddl2cpp.
#pragma once

#include "Table0.hpp"
#include "Table1.hpp"
#include "Table15.hpp"
#include "Table16.hpp"
#include "Table19.hpp"
#include "Table2.hpp"
#include "Table26.hpp"
#include "Table3.hpp"
#include "Table40.hpp"
#include "Table42.hpp"
#include "Table46.hpp"
#include "Table5.hpp"
#include "Table51.hpp"
#include "Table58.hpp"
#include "Table6.hpp"
#include "Table67.hpp"
#include "Table68.hpp"
#include "Table7.hpp"
#include "Table71.hpp"
#include "Table73.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Table77 final
{
    static constexpr std::string_view TableName = "table_77";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "id" }> id;
    Light::Field<Light::SqlTime, Light::SqlRealName { "col_0" }> col0;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_1" }> col1;
    Light::Field<Light::SqlDynamicAnsiString<0>, Light::SqlRealName { "col_2" }> col2;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_3" }> col3;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_4" }> col4;
    Light::Field<Light::SqlDateTime, Light::SqlRealName { "col_5" }> col5;
    Light::Field<std::optional<Light::SqlDynamicAnsiString<0>>, Light::SqlRealName { "col_6" }> col6;
    Light::Field<Light::SqlDate, Light::SqlRealName { "col_7" }> col7;
    Light::Field<Light::SqlAnsiString<50>, Light::SqlRealName { "col_8" }> col8;
    Light::Field<double, Light::SqlRealName { "col_9" }> col9;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_10" }> col10;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_11" }> col11;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_12" }> col12;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_13" }> col13;
    Light::Field<std::optional<Light::SqlTime>, Light::SqlRealName { "col_14" }> col14;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_15" }> col15;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "col_16" }> col16;
    Light::Field<std::optional<Light::SqlDateTime>, Light::SqlRealName { "col_17" }> col17;
    Light::Field<std::optional<Light::SqlAnsiString<50>>, Light::SqlRealName { "col_18" }> col18;
    Light::BelongsTo<&Table42::id, Light::SqlRealName { "fk_42" }> fk42;
    Light::BelongsTo<&Table7::id, Light::SqlRealName { "fk_7" }, Light::SqlNullable::Null> fk7;
    Light::BelongsTo<&Table2::id, Light::SqlRealName { "fk_2" }> fk2;
    Light::BelongsTo<&Table6::id, Light::SqlRealName { "fk_6" }> fk6;
    Light::BelongsTo<&Table0::id, Light::SqlRealName { "fk_0" }, Light::SqlNullable::Null> fk0;
    Light::BelongsTo<&Table26::id, Light::SqlRealName { "fk_26" }> fk26;
    Light::BelongsTo<&Table5::id, Light::SqlRealName { "fk_5" }, Light::SqlNullable::Null> fk5;
    Light::BelongsTo<&Table46::id, Light::SqlRealName { "fk_46" }, Light::SqlNullable::Null> fk46;
    Light::BelongsTo<&Table19::id, Light::SqlRealName { "fk_19" }, Light::SqlNullable::Null> fk19;
    Light::BelongsTo<&Table68::id, Light::SqlRealName { "fk_68" }> fk68;
    Light::BelongsTo<&Table71::id, Light::SqlRealName { "fk_71" }> fk71;
    Light::BelongsTo<&Table73::id, Light::SqlRealName { "fk_73" }, Light::SqlNullable::Null> fk73;
    Light::BelongsTo<&Table58::id, Light::SqlRealName { "fk_58" }, Light::SqlNullable::Null> fk58;
    Light::BelongsTo<&Table51::id, Light::SqlRealName { "fk_51" }, Light::SqlNullable::Null> fk51;
    Light::BelongsTo<&Table15::id, Light::SqlRealName { "fk_15" }> fk15;
    Light::BelongsTo<&Table40::id, Light::SqlRealName { "fk_40" }, Light::SqlNullable::Null> fk40;
    Light::BelongsTo<&Table3::id, Light::SqlRealName { "fk_3" }, Light::SqlNullable::Null> fk3;
    Light::BelongsTo<&Table67::id, Light::SqlRealName { "fk_67" }, Light::SqlNullable::Null> fk67;
    Light::BelongsTo<&Table16::id, Light::SqlRealName { "fk_16" }> fk16;
    Light::BelongsTo<&Table1::id, Light::SqlRealName { "fk_1" }> fk1;
};

