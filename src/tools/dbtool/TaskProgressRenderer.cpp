// SPDX-License-Identifier: Apache-2.0

#include "ProgressText.hpp"
#include "TaskProgressRenderer.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <ranges>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace Lightweight::Tools
{

namespace
{

    constexpr auto UnicodeSpinner = std::array<std::string_view, 10> { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" };
    constexpr auto AsciiSpinner = std::array<std::string_view, 4> { "|", "/", "-", "\\" };
    constexpr int BarWidth = 20;
    constexpr std::size_t IndentPerDepth = 2;
    constexpr std::size_t MinimumTitleWidth = 20;

    [[nodiscard]] std::string_view LevelName(ProgressTask::LogLevel level) noexcept
    {
        switch (level)
        {
            case ProgressTask::LogLevel::Info:
                return "info";
            case ProgressTask::LogLevel::Warning:
                return "warning";
            case ProgressTask::LogLevel::Error:
                return "error";
        }
        return "info";
    }

    [[nodiscard]] std::chrono::milliseconds MillisecondsBetween(std::chrono::steady_clock::time_point from,
                                                                std::chrono::steady_clock::time_point to) noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(to - from);
    }

} // namespace

/// One task as the renderer tracks it.
struct TaskNode
{
    std::uint64_t id {};
    int depth {};
    std::string title;
    std::string path; ///< Titles from the top joined by " > ".
    std::uint64_t total {};
    std::uint64_t done {};
    std::string detail;
    std::optional<std::string> failure; ///< The reason, once the task failed.
    std::chrono::steady_clock::time_point started;
    std::optional<std::chrono::steady_clock::time_point> finished;
};

struct TaskProgressRenderer::State
{
    State(TaskProgressStyle drawStyle, std::ostream& out, ProgressDestination destination, SteadyClockSource clockSource):
        style { drawStyle },
        output { std::make_unique<OStreamTerminalOutput>(out, destination) },
        clock { clockSource ? std::move(clockSource)
                            : SteadyClockSource { [] { return std::chrono::steady_clock::now(); } } },
        start { clock() }
    {
    }

    TaskProgressStyle style;
    std::unique_ptr<OStreamTerminalOutput> output;
    SteadyClockSource clock;
    std::chrono::steady_clock::time_point start;
    mutable std::mutex mutex;
    std::vector<TaskNode> nodes; ///< Tree order: a child directly after its parent's last descendant.
    std::uint64_t nextId = 1;
    int paintedLines = 0; ///< Lines of the block on screen, below which the cursor sits.
    std::size_t frames = 0;
    std::condition_variable_any tick;
    std::jthread ticker; ///< Only for Unicode and Ascii on a terminal.

    /// @return Whether the style draws a repainted block (as opposed to log lines or nothing).
    [[nodiscard]] bool DrawsBlock() const noexcept
    {
        return style == TaskProgressStyle::Unicode || style == TaskProgressStyle::Ascii;
    }

    // Each of these takes the lock itself.
    std::uint64_t Begin(std::uint64_t parent, std::string_view title, std::uint64_t total);
    void SetTotal(std::uint64_t id, std::uint64_t total);
    void Advance(std::uint64_t id, std::uint64_t count, std::string_view detail);
    void End(std::uint64_t id);
    void Fail(std::uint64_t id, std::string_view reason);
    void Log(std::uint64_t id, ProgressTask::LogLevel level, std::string_view message);

    // Callers hold the lock.
    [[nodiscard]] std::vector<TaskNode>::iterator Find(std::uint64_t id);
    [[nodiscard]] std::string LineOf(TaskNode const& node,
                                     std::size_t titleWidth,
                                     std::chrono::steady_clock::time_point now) const;
    void PaintLocked();
    void PrintLineLocked(std::string_view line) const;
};

namespace
{

    /// A task a host opened through the renderer; destroying it finishes the node.
    class RenderedTask final: public ProgressTask
    {
      public:
        RenderedTask(std::shared_ptr<TaskProgressRenderer::State> state, std::uint64_t id):
            _state { std::move(state) },
            _id { id }
        {
        }

        ~RenderedTask() override
        {
            _state->End(_id);
        }

        [[nodiscard]] std::unique_ptr<ProgressTask> BeginSubTask(std::string_view title, std::uint64_t total) override
        {
            return std::make_unique<RenderedTask>(_state, _state->Begin(_id, title, total));
        }

        void SetTotal(std::uint64_t total) override
        {
            _state->SetTotal(_id, total);
        }

        void Advance(std::uint64_t count, std::string_view detail) override
        {
            _state->Advance(_id, count, detail);
        }

        void Log(LogLevel level, std::string_view message) override
        {
            _state->Log(_id, level, message);
        }

        [[nodiscard]] bool IsCancellationRequested() const override
        {
            return false;
        }

        void Fail(std::string_view reason) override
        {
            _state->Fail(_id, reason);
        }

        RenderedTask(RenderedTask const&) = delete;
        RenderedTask(RenderedTask&&) = delete;
        RenderedTask& operator=(RenderedTask const&) = delete;
        RenderedTask& operator=(RenderedTask&&) = delete;

      private:
        std::shared_ptr<TaskProgressRenderer::State> _state;
        std::uint64_t _id;
    };

} // namespace

std::vector<TaskNode>::iterator TaskProgressRenderer::State::Find(std::uint64_t id)
{
    return std::ranges::find(nodes, id, &TaskNode::id);
}

std::uint64_t TaskProgressRenderer::State::Begin(std::uint64_t parent, std::string_view title, std::uint64_t total)
{
    auto const lock = std::scoped_lock { mutex };
    auto node = TaskNode {};
    node.id = nextId++;
    node.title = std::string { title };
    node.path = node.title;
    node.total = total;
    node.started = clock();

    auto insertAt = nodes.end();
    if (auto const parentIt = Find(parent); parent != 0 && parentIt != nodes.end())
    {
        node.depth = parentIt->depth + 1;
        node.path = std::format("{} > {}", parentIt->path, title);
        insertAt =
            std::find_if(parentIt + 1, nodes.end(), [&](TaskNode const& next) { return next.depth <= parentIt->depth; });
    }
    auto const id = node.id;
    auto const path = node.path;
    nodes.insert(insertAt, std::move(node));

    if (style == TaskProgressStyle::LogLine)
        PrintLineLocked(total > 0 ? std::format("begin {}: 0/{}", path, total) : std::format("begin {}", path));
    else if (DrawsBlock())
        PaintLocked();
    return id;
}

void TaskProgressRenderer::State::SetTotal(std::uint64_t id, std::uint64_t total)
{
    auto const lock = std::scoped_lock { mutex };
    auto const it = Find(id);
    if (it == nodes.end())
        return;
    it->total = total;
    if (total > 0)
        it->done = std::min(it->done, total);
    if (style == TaskProgressStyle::LogLine)
        PrintLineLocked(std::format("{}: {}/{}", it->path, it->done, total));
    else if (DrawsBlock())
        PaintLocked();
}

void TaskProgressRenderer::State::Advance(std::uint64_t id, std::uint64_t count, std::string_view detail)
{
    auto const lock = std::scoped_lock { mutex };
    auto const it = Find(id);
    if (it == nodes.end())
        return;
    it->done += count;
    if (it->total > 0)
        it->done = std::min(it->done, it->total);
    if (!detail.empty())
        it->detail = std::string { detail };

    if (style == TaskProgressStyle::LogLine)
    {
        if (count == 0 && detail.empty())
            return;
        if (it->total > 0)
        {
            auto line = std::format("{}: {}/{} ({:.1f}%)",
                                    it->path,
                                    it->done,
                                    it->total,
                                    static_cast<double>(it->done) * 100.0 / static_cast<double>(it->total));
            if (!detail.empty())
                line += std::format(" {}", detail);
            PrintLineLocked(line);
        }
        else if (!detail.empty())
            PrintLineLocked(std::format("{}: {}", it->path, detail));
    }
    else if (DrawsBlock())
        PaintLocked();
}

void TaskProgressRenderer::State::End(std::uint64_t id)
{
    auto const lock = std::scoped_lock { mutex };
    auto const it = Find(id);
    if (it == nodes.end())
        return;
    auto const now = clock();
    it->finished = now;
    if (style == TaskProgressStyle::LogLine)
    {
        auto const elapsed = FormatElapsed(MillisecondsBetween(it->started, now));
        if (it->failure)
            PrintLineLocked(std::format("failed {} ({}): {}", it->path, elapsed, *it->failure));
        else
            PrintLineLocked(std::format("done {} ({})", it->path, elapsed));
    }

    if (it->depth > 0 && !it->failure)
        nodes.erase(it);

    auto const allFinished = std::ranges::all_of(nodes, [](TaskNode const& node) { return node.finished.has_value(); });
    if (DrawsBlock())
        PaintLocked();
    if (allFinished)
    {
        nodes.clear();
        paintedLines = 0;
    }
}

void TaskProgressRenderer::State::Fail(std::uint64_t id, std::string_view reason)
{
    auto const lock = std::scoped_lock { mutex };
    auto const it = Find(id);
    if (it == nodes.end())
        return;
    it->failure = std::string { reason };
    it->detail = std::string { reason };
    if (DrawsBlock())
        PaintLocked();
}

void TaskProgressRenderer::State::Log(std::uint64_t id, ProgressTask::LogLevel level, std::string_view message)
{
    auto const lock = std::scoped_lock { mutex };
    switch (style)
    {
        case TaskProgressStyle::None:
            return;
        case TaskProgressStyle::LogLine: {
            auto const it = id != 0 ? Find(id) : nodes.end();
            if (it != nodes.end())
                PrintLineLocked(std::format("{} {}: {}", LevelName(level), it->path, message));
            else
                PrintLineLocked(std::format("{} {}", LevelName(level), message));
            return;
        }
        case TaskProgressStyle::Unicode:
        case TaskProgressStyle::Ascii:
            break;
    }

    auto const frame = output->syncGuard();
    if (paintedLines > 0)
        output->moveUp(paintedLines);
    output->carriageReturn();
    output->clearToEndOfLine();
    output->writeRaw(std::format("{} {}", LevelName(level), message));
    output->linefeed();
    paintedLines = 0;
    PaintLocked();
}

std::string TaskProgressRenderer::State::LineOf(TaskNode const& node,
                                                std::size_t titleWidth,
                                                std::chrono::steady_clock::time_point now) const
{
    auto const unicode = style == TaskProgressStyle::Unicode;
    auto const end = node.finished.value_or(now);

    auto icon = std::string_view {};
    if (node.failure)
        icon = unicode ? "\u274C" : "x";
    else if (node.finished)
        icon = unicode ? "✅" : "+";
    else if (node.total > 0)
        icon = unicode ? "🔄" : "*";
    else
    {
        auto const frameIndex = static_cast<std::size_t>((end - node.started) / RepaintInterval);
        icon = unicode ? UnicodeSpinner[frameIndex % UnicodeSpinner.size()] : AsciiSpinner[frameIndex % AsciiSpinner.size()];
    }

    auto const label =
        std::format("{}{}", std::string(static_cast<std::size_t>(node.depth) * IndentPerDepth, ' '), node.title);
    auto line = std::format("{} {} {:<{}} ", icon, FormatElapsed(MillisecondsBetween(node.started, end)), label, titleWidth);
    if (node.total > 0)
    {
        auto const shown = std::min(node.done, node.total);
        auto const percent = static_cast<double>(shown) * 100.0 / static_cast<double>(node.total);
        auto const filled = static_cast<int>(shown * BarWidth / node.total);
        line += RenderBarCells(filled, BarWidth, unicode ? BarGlyphs::Unicode : BarGlyphs::Ascii);
        line += std::format("{:5.2f}% ({}/{})", percent, node.done, node.total);
        if (!node.detail.empty())
            line += std::format(" {}", node.detail);
    }
    else
        line += node.detail;
    return line;
}

void TaskProgressRenderer::State::PaintLocked()
{
    auto const now = clock();
    auto titleWidth = MinimumTitleWidth;
    for (auto const& node: nodes)
        titleWidth = std::max(titleWidth, (static_cast<std::size_t>(node.depth) * IndentPerDepth) + node.title.size());

    auto lines = std::vector<std::string> {};
    lines.reserve(nodes.size());
    for (auto const& node: nodes)
        lines.push_back(LineOf(node, titleWidth, now));

    auto const frame = output->syncGuard();
    if (paintedLines > 0)
        output->moveUp(paintedLines);
    for (auto const& line: lines)
    {
        output->carriageReturn();
        output->clearToEndOfLine();
        output->writeRaw(line);
        output->linefeed();
    }
    auto const lineCount = static_cast<int>(lines.size());
    if (auto const leftovers = paintedLines - lineCount; leftovers > 0)
    {
        for (auto const leftover: std::views::iota(0, leftovers))
        {
            (void) leftover;
            output->clearToEndOfLine();
            output->linefeed();
        }
        output->moveUp(leftovers);
    }
    paintedLines = lineCount;
    ++frames;
}

void TaskProgressRenderer::State::PrintLineLocked(std::string_view line) const
{
    auto const frame = output->syncGuard();
    output->writeRaw(std::format("[{}] {}\n", FormatElapsed(MillisecondsBetween(start, clock())), line));
}

TaskProgressRenderer::TaskProgressRenderer(TaskProgressStyle style,
                                           std::ostream& out,
                                           ProgressDestination destination,
                                           SteadyClockSource clock):
    _state { std::make_shared<State>(style, out, destination, std::move(clock)) }
{
    if (!_state->DrawsBlock() || destination != ProgressDestination::Terminal)
        return;
    _state->ticker = std::jthread { [state = _state.get()](std::stop_token const& stop) {
        auto lock = std::unique_lock { state->mutex };
        while (!state->tick.wait_for(lock, stop, RepaintInterval, [] { return false; }))
        {
            if (stop.stop_requested())
                return;
            if (!state->nodes.empty())
                state->PaintLocked();
        }
    } };
}

TaskProgressRenderer::TaskProgressRenderer(TaskProgressStyle style):
    TaskProgressRenderer(style, std::cout, DestinationOf(std::cout))
{
}

TaskProgressRenderer::~TaskProgressRenderer()
{
    if (_state->ticker.joinable())
    {
        _state->ticker.request_stop();
        _state->ticker.join();
    }
    auto const lock = std::scoped_lock { _state->mutex };
    if (_state->DrawsBlock() && !_state->nodes.empty())
        _state->PaintLocked();
}

std::unique_ptr<ProgressTask> TaskProgressRenderer::BeginSubTask(std::string_view title, std::uint64_t total)
{
    return std::make_unique<RenderedTask>(_state, _state->Begin(0, title, total));
}

void TaskProgressRenderer::SetTotal(std::uint64_t /*total*/) {}

void TaskProgressRenderer::Advance(std::uint64_t /*count*/, std::string_view /*detail*/) {}

void TaskProgressRenderer::Fail(std::string_view /*reason*/) {}

void TaskProgressRenderer::Log(LogLevel level, std::string_view message)
{
    _state->Log(0, level, message);
}

bool TaskProgressRenderer::IsCancellationRequested() const
{
    return false;
}

void TaskProgressRenderer::Repaint()
{
    auto const lock = std::scoped_lock { _state->mutex };
    if (_state->DrawsBlock())
        _state->PaintLocked();
}

std::size_t TaskProgressRenderer::FramesWritten() const
{
    auto const lock = std::scoped_lock { _state->mutex };
    return _state->frames;
}

} // namespace Lightweight::Tools
