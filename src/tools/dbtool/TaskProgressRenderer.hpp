// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "StandardProgressManager.hpp"

#include <Lightweight/ProgressTask.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string_view>

namespace Lightweight::Tools
{

/// How a TaskProgressRenderer draws; follows dbtool's `--progress` (and `--quiet` = None).
enum class TaskProgressStyle : std::uint8_t
{
    Unicode, ///< Bars with FiraCode's progress glyphs, a braille spinner for indeterminate tasks.
    Ascii,   ///< `[===...]` bars, a `|/-\` spinner.
    LogLine, ///< One line per event, for CI and log files.
    None,    ///< Nothing.
};

/// The clock a TaskProgressRenderer measures elapsed time with; empty means `steady_clock::now`.
using SteadyClockSource = std::function<std::chrono::steady_clock::time_point()>;

/// @brief Draws a tree of ProgressTasks to a stream; the renderer itself is the invisible root task.
///
/// Unicode and Ascii keep a block of lines, one per task in tree order, indented by depth, and
/// repaint it in place on every event; on a terminal a ticker also repaints every RepaintInterval,
/// so spinners turn and elapsed times run while a statement reports nothing. A finished sub-task's
/// line disappears; a finished top-level line stays until no task is active, when the block is
/// final and later output (and a later task) goes below it. Nothing else may write to the stream
/// while a task is active; route such messages through `Log`, which prints them above the block.
/// Every method is thread-safe; tasks must not outlive the renderer.
class TaskProgressRenderer final: public ProgressTask
{
  public:
    /// The ticker's period, and one spinner frame.
    static constexpr auto RepaintInterval = std::chrono::milliseconds { 200 };

    /// Draws to @p out, which is what @p destination says it is.
    /// @param style How to draw.
    /// @param out The stream to draw to.
    /// @param destination Whether @p out is a terminal; only a terminal gets the ticker.
    /// @param clock The clock elapsed times are measured with.
    TaskProgressRenderer(TaskProgressStyle style,
                         std::ostream& out,
                         ProgressDestination destination,
                         SteadyClockSource clock = {});

    /// Draws to std::cout, a terminal when the process's standard output is one.
    /// @param style How to draw.
    explicit TaskProgressRenderer(TaskProgressStyle style);

    TaskProgressRenderer(TaskProgressRenderer const&) = delete;
    TaskProgressRenderer(TaskProgressRenderer&&) = delete;
    TaskProgressRenderer& operator=(TaskProgressRenderer const&) = delete;
    TaskProgressRenderer& operator=(TaskProgressRenderer&&) = delete;

    /// Stops the ticker and draws the final frame.
    ~TaskProgressRenderer() override;

    /// Opens a top-level task.
    [[nodiscard]] std::unique_ptr<ProgressTask> BeginSubTask(std::string_view title, std::uint64_t total) override;
    /// The root has no bar: does nothing.
    void SetTotal(std::uint64_t total) override;
    /// The root has no bar: does nothing.
    void Advance(std::uint64_t count, std::string_view detail) override;
    /// Prints @p message above the block (LogLine: `[elapsed] <level> <message>`).
    void Log(LogLevel level, std::string_view message) override;
    /// @return Always false; dbtool does not cancel.
    [[nodiscard]] bool IsCancellationRequested() const override;
    /// The root has no row: does nothing.
    void Fail(std::string_view reason) override;

    /// Repaints the block now; the ticker calls it, tests call it to see a frame.
    void Repaint();

    /// @return How many frames were painted so far (tests).
    [[nodiscard]] std::size_t FramesWritten() const;

    /// The renderer's shared state; defined in the implementation file.
    struct State;

  private:
    std::shared_ptr<State> _state;
};

} // namespace Lightweight::Tools
