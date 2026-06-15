// File is automatically generated using ddl2cpp.
#pragma once

#if !defined(LIGHTWEIGHT_BUILD_MODULES)
    #include <Lightweight/DataMapper/DataMapper.hpp>
#endif

#include <array>
#include <string_view>

struct Playlist final
{
    static constexpr std::string_view TableName = "Playlist";

    Light::Field<int32_t, Light::PrimaryKey::ServerSideAutoIncrement, Light::SqlRealName { "PlaylistId" }> PlaylistId;
    Light::Field<std::optional<Light::SqlDynamicUtf16String<120>>, Light::SqlRealName { "Name" }> Name;
};

template <>
struct Lightweight::Description<Playlist>
{
    static constexpr std::size_t FieldCount = 2;
    using Members = Lightweight::RecordMemberList<&Playlist::PlaylistId, &Playlist::Name>;
    static constexpr std::array<std::string_view, 2> FieldNames = { "PlaylistId", "Name" };
};
