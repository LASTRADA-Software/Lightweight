// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Artist final
{
    static constexpr std::string_view TableName = "Artist";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"ArtistId"}> ArtistId;
    Field<std::optional<SqlUtf16String<120>>, SqlRealName{"Name"}> Name;
};

