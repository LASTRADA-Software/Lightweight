// File is automatically generated using ddl2cpp.
#pragma once

#include "Artist.hpp"

#if !defined(LIGHTWEIGHT_BUILD_MODULES)
    #include <Lightweight/DataMapper/DataMapper.hpp>
#endif

#include <array>
#include <string_view>

struct Album final
{
    static constexpr std::string_view TableName = "Album";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "AlbumId" }> AlbumId;
    Light::Field<Light::SqlDynamicUtf16String<160>, Light::SqlRealName { "Title" }> Title;
    Light::BelongsTo<&Artist::ArtistId, Light::SqlRealName { "ArtistId" }> ArtistId;
};

template <>
struct Lightweight::Description<Album>
{
    static constexpr std::size_t FieldCount = 3;
    using Members = Lightweight::RecordMemberList<&Album::AlbumId, &Album::Title, &Album::ArtistId>;
    static constexpr std::array<std::string_view, 3> FieldNames = { "AlbumId", "Title", "ArtistId" };
};
