// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>

#include "Playlist.hpp"
#include "Track.hpp"


struct Playlisttrack final
{
    static constexpr std::string_view TableName = "PlaylistTrack";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"PlaylistId"}> PlaylistId;
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"TrackId"}> TrackId;

    BelongsTo<&Playlist::PlaylistId, SqlRealName{"PlaylistId"}> fk_PlaylistId;
    BelongsTo<&Track::TrackId, SqlRealName{"TrackId"}> fk_TrackId;
};

