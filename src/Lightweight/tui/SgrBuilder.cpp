// SPDX-License-Identifier: Apache-2.0
// Vendored from endo/src/tui/SgrBuilder.cpp — kept intentionally close to upstream
// (only header path changed) so re-syncing later is mechanical.

#include "SgrBuilder.hpp"

#include <cstdint>
#include <format>
#include <variant>

namespace tui
{

std::string buildSgrSequence(Style const& style)
{
    auto const underlineFromFlag = style.underline ? UnderlineStyle::Single : UnderlineStyle::None;
    auto const effectiveUnderline = style.underlineStyle != UnderlineStyle::None ? style.underlineStyle : underlineFromFlag;
    auto const isDefaultUlColor = std::holds_alternative<std::monostate>(style.underlineColor);

    auto const isDefaultFg = std::holds_alternative<std::monostate>(style.fg);
    auto const isDefaultBg = std::holds_alternative<std::monostate>(style.bg);
    if (isDefaultFg && isDefaultBg && !style.bold && !style.italic && !style.strikethrough && !style.dim && !style.inverse
        && effectiveUnderline == UnderlineStyle::None)
        return {};

    std::string result = "\033[";
    auto needSemicolon = false;
    auto const appendSep = [&]() {
        if (needSemicolon)
            result += ';';
        needSemicolon = true;
    };

    if (style.bold)
    {
        appendSep();
        result += '1';
    }
    if (style.dim)
    {
        appendSep();
        result += '2';
    }
    if (style.italic)
    {
        appendSep();
        result += '3';
    }
    if (effectiveUnderline != UnderlineStyle::None)
    {
        appendSep();
        result += std::format("4:{}", static_cast<int>(effectiveUnderline));
    }
    if (style.inverse)
    {
        appendSep();
        result += '7';
    }
    if (style.strikethrough)
    {
        appendSep();
        result += '9';
    }

    if (auto const* idx = std::get_if<std::uint8_t>(&style.fg))
    {
        appendSep();
        result += std::format("38;5;{}", *idx);
    }
    else if (auto const* rgb = std::get_if<RgbColor>(&style.fg))
    {
        appendSep();
        result += std::format("38;2;{};{};{}", rgb->r, rgb->g, rgb->b);
    }

    if (auto const* idx = std::get_if<std::uint8_t>(&style.bg))
    {
        appendSep();
        result += std::format("48;5;{}", *idx);
    }
    else if (auto const* rgb = std::get_if<RgbColor>(&style.bg))
    {
        appendSep();
        result += std::format("48;2;{};{};{}", rgb->r, rgb->g, rgb->b);
    }

    result += 'm';

    if (!isDefaultUlColor)
    {
        if (auto const* idx = std::get_if<std::uint8_t>(&style.underlineColor))
            result += std::format("\033[58:5:{}m", *idx);
        else if (auto const* rgb = std::get_if<RgbColor>(&style.underlineColor))
            result += std::format("\033[58:2:{}:{}:{}m", rgb->r, rgb->g, rgb->b);
    }

    return result;
}

} // namespace tui
