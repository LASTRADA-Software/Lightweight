// SPDX-License-Identifier: Apache-2.0
// Vendored from endo/src/tui/Unicode.cpp.

#include "Unicode.hpp"

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace tui
{

int stringWidth(std::string_view text) noexcept
{
    auto width = 0;
    auto segmenter = unicode::utf8_grapheme_segmenter(text);
    for (auto const& cluster: segmenter)
        width += graphemeClusterWidth(cluster);
    return width;
}

} // namespace tui
