// File is automatically generated using ddl2cpp.
#pragma once

#if !defined(LIGHTWEIGHT_BUILD_MODULES)
    #include <Lightweight/DataMapper/DataMapper.hpp>
#endif

struct Playlisttrack final
{
    static constexpr std::string_view TableName = "PlaylistTrack";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "PlaylistId" }>
        PlaylistId; // NB: This is also a foreign key
    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "TrackId" }>
        TrackId; // NB: This is also a foreign key
};
