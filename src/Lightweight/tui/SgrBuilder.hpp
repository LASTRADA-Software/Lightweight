// SPDX-License-Identifier: Apache-2.0
// Vendored from endo/src/tui/SgrBuilder.hpp.

#pragma once

#include "Style.hpp"

#include <string>

namespace tui
{

// NOLINTBEGIN(readability-identifier-naming)
// Vendored upstream API names (camelCase) preserved verbatim — keeps re-syncs mechanical.

/// Builds a complete SGR (Select Graphic Rendition) escape sequence for the given style.
///
/// Encodes bold, dim, italic, underline (with extended styles), inverse, strikethrough,
/// foreground/background colors (256-color and truecolor), and underline color (SGR 58).
/// Returns an empty string if the style is fully default.
[[nodiscard]] std::string buildSgrSequence(Style const& style);

/// Returns the universal SGR reset sequence: `ESC [ 0 m`.
[[nodiscard]] inline std::string sgrReset()
{
    return "\033[0m";
}

// NOLINTEND(readability-identifier-naming)

} // namespace tui
