// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"

#include <cstdlib>
#include <string>

namespace Lightweight
{

// Specialized traits for std::basic_string<> as output string parameter
template <typename CharT>
struct SqlBasicStringOperations<std::basic_string<CharT>>
{
    using CharType = CharT;
    using StringType = std::basic_string<CharT>;

    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Varchar { 255 };

    static LIGHTWEIGHT_FORCE_INLINE CharType const* Data(StringType const* str) noexcept
    {
        return str->data();
    }

    static LIGHTWEIGHT_FORCE_INLINE CharType* Data(StringType* str) noexcept
    {
        return str->data();
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLULEN Size(StringType const* str) noexcept
    {
        return str->size();
    }

    static LIGHTWEIGHT_FORCE_INLINE void Clear(StringType* str) noexcept
    {
        str->clear();
    }

    static LIGHTWEIGHT_FORCE_INLINE void Reserve(StringType* str, size_t capacity) noexcept
    {
        // std::basic_string<> tries to defer the allocation as long as possible.
        // So we first tell StringType how much to reserve and then resize it to the *actually* reserved
        // size.
        str->reserve(capacity);
        str->resize(str->capacity());
    }

    static LIGHTWEIGHT_FORCE_INLINE void Resize(StringType* str, SQLLEN indicator) noexcept
    {
        if (indicator >= 0)
            str->resize(static_cast<size_t>(indicator));
    }
};

} // namespace Lightweight
