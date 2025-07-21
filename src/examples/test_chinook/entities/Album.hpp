// File is automatically generated using ddl2cpp.
#pragma once

#include "Artist.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Album final
{
    static constexpr std::string_view TableName = "Album";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "AlbumId" }> AlbumId;
    Light::Field<Light::SqlDynamicUtf16String<160>, Light::SqlRealName { "Title" }> Title;
    Light::BelongsTo<&Artist::ArtistId, Light::SqlRealName { "ArtistId" }> ArtistId;
};

