// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Genre final
{
    static constexpr std::string_view TableName = "Genre";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"GenreId"}> GenreId;
    Field<std::optional<SqlDynamicUtf16String<120>>, SqlRealName{"Name"}> Name;
};

