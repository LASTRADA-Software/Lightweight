// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Playlist final
{
    static constexpr std::string_view TableName = "Playlist";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"PlaylistId"}> PlaylistId;
    Field<std::optional<SqlDynamicUtf16String<120>>, SqlRealName{"Name"}> Name;
};

