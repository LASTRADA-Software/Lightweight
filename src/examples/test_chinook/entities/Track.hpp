// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>

#include "Album.hpp"
#include "Genre.hpp"
#include "Mediatype.hpp"


struct Track final
{
    static constexpr std::string_view TableName = "Track";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"TrackId"}> TrackId;
    Field<SqlUtf16String<200>, SqlRealName{"Name"}> Name;
    BelongsTo<&Album::AlbumId, SqlRealName{"AlbumId"}> AlbumId;
    BelongsTo<&Mediatype::MediaTypeId, SqlRealName{"MediaTypeId"}> MediaTypeId;
    BelongsTo<&Genre::GenreId, SqlRealName{"GenreId"}> GenreId;
    Field<std::optional<SqlUtf16String<220>>, SqlRealName{"Composer"}> Composer;
    Field<int32_t, SqlRealName{"Milliseconds"}> Milliseconds;
    Field<std::optional<int32_t>, SqlRealName{"Bytes"}> Bytes;
    Field<SqlNumeric<10, 2>, SqlRealName{"UnitPrice"}> UnitPrice;
};

