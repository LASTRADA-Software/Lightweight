// File is automatically generated using ddl2cpp.
#pragma once

#include <Lightweight/DataMapper/DataMapper.hpp>

struct Mediatype final
{
    static constexpr std::string_view TableName = "MediaType";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "MediaTypeId" }> MediaTypeId;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<120>>, Light::SqlRealName { "Name" }> Name;
};
