// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "StandardProgressManager.hpp"

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <string>

#include <core/tui/TerminalOutput.hpp>

namespace Lightweight::Tools
{

/// core-cpp's terminal output, writing to an injected stream instead of the process's standard
/// output. Composing the escape sequences is core-cpp's; this only says where the bytes go and
/// whether that is a terminal.
class OStreamTerminalOutput final: public core::tui::TerminalOutput
{
  public:
    /// Writes to @p out, which is what @p destination says it is.
    /// @param out The stream the bytes go to.
    /// @param destination Whether @p out is a terminal.
    OStreamTerminalOutput(std::ostream& out, ProgressDestination destination);

    /// @return True when the destination is a terminal.
    [[nodiscard]] bool isTerminal() const noexcept override;

  protected:
    /// Writes @p bytes to the stream and flushes it.
    void writeToDestination(std::string_view bytes) override;

  private:
    std::ostream& _out;
    ProgressDestination _destination;
};

/// Whether @p out is a terminal: only std::cout can be, and only when the operating system says
/// the process's standard output is one.
/// @param out The stream to classify.
/// @return The destination @p out is.
[[nodiscard]] ProgressDestination DestinationOf(std::ostream const& out);

/// Which glyphs draw a progress bar.
enum class BarGlyphs : std::uint8_t
{
    Unicode, ///< FiraCode's progress glyphs (U+EE00..U+EE05).
    Ascii,   ///< `[==...]`.
};

/// The cells of a progress bar followed by one space.
/// @param filled Filled cells, 0..@p width.
/// @param width Cells of the bar.
/// @param glyphs The glyphs to draw with.
/// @return The bar text.
[[nodiscard]] std::string RenderBarCells(int filled, int width, BarGlyphs glyphs);

/// @param elapsed A duration.
/// @return `HH:MM:SS.mmm`.
[[nodiscard]] std::string FormatElapsed(std::chrono::milliseconds elapsed);

} // namespace Lightweight::Tools
