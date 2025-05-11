// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>

#include "Artist.hpp"


struct Album final
{
    static constexpr std::string_view TableName = "Album";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"AlbumId"}> AlbumId;
    Field<SqlUtf16String<160>, SqlRealName{"Title"}> Title;
    BelongsTo<&Artist::ArtistId, SqlRealName{"ArtistId"}> ArtistId;
};

