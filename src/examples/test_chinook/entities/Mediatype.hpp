// File is automatically generated using ddl2cpp.
#pragma once

struct Mediatype final
{
    static constexpr string_view TableName = "MediaType";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName { "MediaTypeId" }> MediaTypeId;
    Field<optional<SqlDynamicUtf16String<120>>, SqlRealName { "Name" }> Name;
};
