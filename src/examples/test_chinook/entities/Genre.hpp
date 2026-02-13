// File is automatically generated using ddl2cpp.
#pragma once

struct Genre final
{
    static constexpr string_view TableName = "Genre";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "GenreId" }> GenreId;
    Field<optional<SqlDynamicUtf16String<120>>, SqlRealName { "Name" }> Name;
};
