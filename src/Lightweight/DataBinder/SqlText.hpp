// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"
#include "StdString.hpp"

#include <string>

namespace Lightweight
{

/// Represents a TEXT field in a SQL database.
///
/// This is used for large texts, e.g. up to 65k characters.
///
/// @ingroup DataTypes
struct SqlText
{
    /// The underlying value type.
    using value_type = std::string;

    /// The text value.
    value_type value;

    /// Three-way comparison operator.
    std::weak_ordering operator<=>(SqlText const&) const noexcept = default;
};

template <>
struct SqlBasicStringOperations<SqlText>
{
    using Traits = SqlBasicStringOperations<SqlText::value_type>;

    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Text {};

    // clang-format off
    static LIGHTWEIGHT_FORCE_INLINE char const* Data(SqlText const* str) noexcept { return Traits::Data(&str->value); }
    static LIGHTWEIGHT_FORCE_INLINE char* Data(SqlText* str) noexcept { return Traits::Data(&str->value); }
    static LIGHTWEIGHT_FORCE_INLINE SQLULEN Size(SqlText const* str) noexcept { return Traits::Size(&str->value); }
    static LIGHTWEIGHT_FORCE_INLINE void Clear(SqlText* str) noexcept { Traits::Clear(&str->value); }
    static LIGHTWEIGHT_FORCE_INLINE void Reserve(SqlText* str, size_t capacity) noexcept { Traits::Reserve(&str->value, capacity); }
    static LIGHTWEIGHT_FORCE_INLINE void Resize(SqlText* str, SQLLEN indicator) noexcept { Traits::Resize(&str->value, indicator); }
    // clang-format on
};

} // namespace Lightweight

template <>
struct std::formatter<Lightweight::SqlText>: std::formatter<std::string>
{
    LIGHTWEIGHT_FORCE_INLINE auto format(Lightweight::SqlText const& text, format_context& ctx) const
        -> format_context::iterator
    {
        return std::formatter<std::string>::format(text.value, ctx);
    }
};
