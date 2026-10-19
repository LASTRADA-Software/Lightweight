// SPDX-License-Identifier: Apache-2.0

#include "ProgressText.hpp"

#include <format>
#include <iostream>
#include <ranges>

namespace Lightweight::Tools
{

OStreamTerminalOutput::OStreamTerminalOutput(std::ostream& out, ProgressDestination destination):
    _out { out },
    _destination { destination }
{
}

bool OStreamTerminalOutput::isTerminal() const noexcept
{
    return _destination == ProgressDestination::Terminal;
}

void OStreamTerminalOutput::writeToDestination(std::string_view bytes)
{
    _out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    _out.flush();
}

ProgressDestination DestinationOf(std::ostream const& out)
{
    if (&out != &std::cout || !core::tui::TerminalOutput {}.isTerminal())
        return ProgressDestination::NotTerminal;
    return ProgressDestination::Terminal;
}

std::string RenderBarCells(int filled, int width, BarGlyphs glyphs)
{
    auto bar = std::string {};
    if (glyphs == BarGlyphs::Unicode)
    {
        // FiraCode progress bar: U+EE00..U+EE02 empty left/middle/right, U+EE03..U+EE05 filled.
        bar += filled > 0 ? "\uEE03" : "\uEE00";
        for (auto const cell: std::views::iota(0, width))
            bar += cell < filled ? "\uEE04" : "\uEE01";
        bar += filled == width ? "\uEE05 " : "\uEE02 ";
        return bar;
    }
    bar += "[";
    for (auto const cell: std::views::iota(0, width))
        bar += cell < filled ? "=" : ".";
    bar += "] ";
    return bar;
}

std::string FormatElapsed(std::chrono::milliseconds elapsed)
{
    auto const total = elapsed.count();
    return std::format(
        "{:02}:{:02}:{:02}.{:03}", total / 3600000, (total % 3600000) / 60000, (total % 60000) / 1000, total % 1000);
}

} // namespace Lightweight::Tools
