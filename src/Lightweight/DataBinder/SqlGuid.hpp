// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BasicStringBinder.hpp"
#include "Core.hpp"
#include "StdString.hpp"

#include <format>
#include <optional>
#include <string>

struct LIGHTWEIGHT_API SqlGuid
{
    uint8_t data[16] {};

    static SqlGuid Create() noexcept;

    static std::optional<SqlGuid> TryParse(std::string_view const& text) noexcept;

    static SqlGuid constexpr UnsafeParse(std::string_view const& text) noexcept;

    constexpr std::weak_ordering operator<=>(SqlGuid const& other) const noexcept = default;
};

constexpr SqlGuid SqlGuid::UnsafeParse(std::string_view const& text) noexcept
{
    SqlGuid guid {};

    // UUID format: xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
    // M is the version and N is the variant

    // Check for length
    if (text.size() != 36)
        return { "\x01" };

    // Check for dashes
    if (text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        return { "\x02" };

    // Version must be 1, 2, 3, 4, or 5
    auto const version = text[14];
    if (!('1' <= version && version <= '5'))
        return { "\x03" };

    // Variant must be 8, 9, A, or B
    auto const variant = text[21];
    if (variant != '8' && variant != '9' && variant != 'A' && variant != 'B' && variant != 'a' && variant != 'b')
        return { "\x04" };

    // clang-format off
    size_t i = 0;
    for (auto const index: { 0, 2, 4, 6,
                             9, 11,
                             14, 16,
                             21, 19,
                             24, 26, 28, 30, 32, 34 })
    {
        if (std::from_chars(text.data() + index, text.data() + index + 2, guid.data[i], 16).ec != std::errc())
            return { "\x05" };
        i++;
    }
    // clang-format on

    return guid;
}

template <>
struct LIGHTWEIGHT_API std::formatter<SqlGuid>: std::formatter<std::string>
{
    auto format(SqlGuid const& guid, format_context& ctx) const -> format_context::iterator
    {
        // clang-format off
        return formatter<std::string>::format(std::format(
                "{:08X}-{:04X}-{:04X}-{:04X}-{:012X}",
                (uint32_t) guid.data[3] | (uint32_t) guid.data[2] << 8 |
                    (uint32_t) guid.data[1] << 16 | (uint32_t) guid.data[0] << 24,
                (uint16_t) guid.data[5] | (uint16_t) guid.data[4] << 8,
                (uint16_t) guid.data[7] | (uint16_t) guid.data[6] << 8,
                (uint16_t) guid.data[8] | (uint16_t) guid.data[9] << 8,
                (uint64_t) guid.data[15] | (uint64_t) guid.data[14] << 8 |
                    (uint64_t) guid.data[13] << 16 | (uint64_t) guid.data[12] << 24 |
                    (uint64_t) guid.data[11] << 32 | (uint64_t) guid.data[10] << 40
            ),
            ctx
        );
        // clang-format on
    }
};

inline LIGHTWEIGHT_FORCE_INLINE std::string to_string(SqlGuid const& guid)
{
    return std::format("{}", guid);
}

template <>
struct LIGHTWEIGHT_API SqlDataBinder<SqlGuid>
{
    static constexpr auto ColumnType = SqlColumnType::GUID;

    static SQLRETURN InputParameter(SQLHSTMT stmt,
                                    SQLUSMALLINT column,
                                    SqlGuid const& value,
                                    SqlDataBinderCallback& cb) noexcept;

    static SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, SqlGuid* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept;

    static SQLRETURN GetColumn(SQLHSTMT stmt,
                               SQLUSMALLINT column,
                               SqlGuid* result,
                               SQLLEN* indicator,
                               SqlDataBinderCallback const& cb) noexcept;

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(SqlGuid const& value) noexcept
    {
        return std::format("{}", value);
    }
};
