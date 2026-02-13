// File is automatically generated using ddl2cpp.
#pragma once

struct Playlisttrack final
{
    static constexpr string_view TableName = "PlaylistTrack";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "PlaylistId" }>
        PlaylistId; // NB: This is also a foreign key
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "TrackId" }>
        TrackId; // NB: This is also a foreign key
};
