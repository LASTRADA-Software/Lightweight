// SPDX-License-Identifier: Apache-2.0
// Vendored from endo/src/tui/Unicode.hpp.

#pragma once

#include <algorithm>
#include <string_view>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/codepoint_properties.h>
#include <libunicode/width.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace tui
{

// NOLINTBEGIN(readability-identifier-naming)
// Vendored upstream API names (camelCase) preserved verbatim — keeps re-syncs mechanical.

/// Calculates the display width of a grapheme cluster.
inline int graphemeClusterWidth(std::u32string_view cluster) noexcept
{
    if (cluster.empty())
        return 0;

    auto const props = unicode::codepoint_properties::get(cluster[0]);

    if (props.is_emoji_presentation())
        return 2;

    if (props.is_emoji() && cluster.size() > 1)
    {
        bool const hasZwj = std::ranges::find(cluster, U'‍') != cluster.end();
        if (hasZwj)
            return 2;
    }

    int const baseWidth = static_cast<int>(unicode::width(cluster[0]));
    return baseWidth > 0 ? baseWidth : 1;
}

/// Calculates the display width of a UTF-8 string in terminal columns.
int stringWidth(std::string_view text) noexcept;

// NOLINTEND(readability-identifier-naming)

} // namespace tui
