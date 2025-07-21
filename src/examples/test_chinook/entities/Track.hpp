// File is automatically generated using ddl2cpp.
#pragma once

#include "Album.hpp"
#include "Genre.hpp"
#include "Mediatype.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Track final
{
    static constexpr std::string_view TableName = "Track";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "TrackId" }> TrackId;
    Light::Field<Light::SqlDynamicUtf16String<200>, Light::SqlRealName { "Name" }> Name;
    Light::BelongsTo<&Album::AlbumId, Light::SqlRealName { "AlbumId" }, Light::SqlNullable::Null> AlbumId;
    Light::BelongsTo<&Mediatype::MediaTypeId, Light::SqlRealName { "MediaTypeId" }> MediaTypeId;
    Light::BelongsTo<&Genre::GenreId, Light::SqlRealName { "GenreId" }, Light::SqlNullable::Null> GenreId;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<220>>, Light::SqlRealName { "Composer" }> Composer;
    Light::Field<int32_t, Light::SqlRealName { "Milliseconds" }> Milliseconds;
    Light::Field<std::optional<int32_t>, Light::SqlRealName { "Bytes" }> Bytes;
    Light::Field<Light::SqlNumeric<10, 2>, Light::SqlRealName { "UnitPrice" }> UnitPrice;
};

