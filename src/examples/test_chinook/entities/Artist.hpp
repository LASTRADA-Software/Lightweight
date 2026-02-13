// File is automatically generated using ddl2cpp.
#pragma once

struct Artist final
{
    static constexpr string_view TableName = "Artist";
    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "ArtistId" }> ArtistId;
    Field<optional<SqlDynamicUtf16String<120>>, SqlRealName { "Name" }> Name;
};
