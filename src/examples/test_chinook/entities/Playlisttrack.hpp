// File is automatically generated using ddl2cpp.
#pragma once

#if !defined(LIGHTWEIGHT_BUILD_MODULES)
    #include <Lightweight/DataMapper/DataMapper.hpp>
#endif

#include <array>
#include <string_view>

struct Playlisttrack final
{
    static constexpr std::string_view TableName = "PlaylistTrack";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "PlaylistId" }>
        PlaylistId; // NB: This is also a foreign key
    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "TrackId" }>
        TrackId; // NB: This is also a foreign key
};

template <>
struct Lightweight::Description<Playlisttrack>
{
    static constexpr std::size_t FieldCount = 2;
    using Members = Lightweight::RecordMemberList<&Playlisttrack::PlaylistId, &Playlisttrack::TrackId>;
    static constexpr std::array<std::string_view, 2> FieldNames = { "PlaylistId", "TrackId" };
};
