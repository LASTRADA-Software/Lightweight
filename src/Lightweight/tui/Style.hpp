// SPDX-License-Identifier: Apache-2.0
//
// Vendored from endo (https://github.com/...) — extracted Style / RgbColor from
// endo's TerminalOutput.hpp so we depend on a small, stable surface rather than
// the full terminal-IO module. Keep the namespace and field names identical so
// the diff against upstream stays trivially reviewable.

#pragma once

#include <cstdint>
#include <variant>

namespace tui
{

/// @brief RGB color representation.
struct RgbColor
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

/// @brief Color representation: default, 256-color index, or true color (RGB).
using Color = std::variant<std::monostate, std::uint8_t, RgbColor>;

/// @brief Underline style for terminal output (SGR 4:n).
enum class UnderlineStyle : std::uint8_t
{
    None = 0,
    Single = 1,
    Double = 2,
    Curly = 3,
    Dotted = 4,
    Dashed = 5,
};

/// @brief Text styling attributes for terminal output.
struct Style
{
    Color fg;
    Color bg;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool dim = false;
    bool inverse = false;
    UnderlineStyle underlineStyle = UnderlineStyle::None;
    Color underlineColor;
};

} // namespace tui
