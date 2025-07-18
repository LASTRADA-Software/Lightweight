// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>


struct Mediatype final
{
    static constexpr std::string_view TableName = "MediaType";

    Field<int32_t, PrimaryKey::ServerSideAutoIncrement, SqlRealName{"MediaTypeId"}> MediaTypeId;
    Field<std::optional<SqlDynamicUtf16String<120>>, SqlRealName{"Name"}> Name;
};

