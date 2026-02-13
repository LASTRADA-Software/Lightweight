// File is automatically generated using ddl2cpp.
#pragma once

struct Playlist final
{
    static constexpr string_view TableName = "Playlist";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "PlaylistId" }> PlaylistId;
    Field<optional<SqlDynamicUtf16String<120>>, SqlRealName { "Name" }> Name;
};
