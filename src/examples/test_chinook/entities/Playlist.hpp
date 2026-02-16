// File is automatically generated using ddl2cpp.
#pragma once

#if !defined(LIGHTWEIGHT_BUILD_MODULES)
#include <Lightweight/DataMapper/DataMapper.hpp>
#endif


struct Playlist final
{
    static constexpr std::string_view TableName = "Playlist";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "PlaylistId" }> PlaylistId;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<120>>, Light::SqlRealName { "Name" }> Name;
};

