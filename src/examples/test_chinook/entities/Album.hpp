// File is automatically generated using ddl2cpp.
#pragma once

#include "Artist.hpp"

struct Album final
{
    static constexpr string_view TableName = "Album";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "AlbumId" }> AlbumId;
    Field<SqlDynamicUtf16String<160>, SqlRealName { "Title" }> Title;
    BelongsTo<&Artist::ArtistId, SqlRealName { "ArtistId" }> ArtistId;
};
