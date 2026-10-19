// SPDX-License-Identifier: Apache-2.0

#include "../../tools/dbtool/TaskProgressRenderer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <sstream>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using Lightweight::ProgressTask;
using Lightweight::Tools::ProgressDestination;
using Lightweight::Tools::TaskProgressRenderer;
using Lightweight::Tools::TaskProgressStyle;

namespace
{

/// A clock the test advances by hand.
struct ManualClock
{
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::time_point {} + 1h;

    [[nodiscard]] Lightweight::Tools::SteadyClockSource Source()
    {
        return [this] {
            return now;
        };
    }
};

} // namespace

TEST_CASE("TaskProgressRenderer prints one line per event in logline style", "[dbtool][TaskProgressRenderer]")
{
    auto clock = ManualClock {};
    auto out = std::ostringstream {};
    {
        auto renderer =
            TaskProgressRenderer { TaskProgressStyle::LogLine, out, ProgressDestination::NotTerminal, clock.Source() };
        auto step = renderer.BeginSubTask("Step", 0);
        step->Advance(0, "planning");
        clock.now += 1500ms;
        {
            auto table = step->BeginSubTask("Table a", 10);
            table->Advance(5, "alter 1 column(s)");
            table->Log(ProgressTask::LogLevel::Warning, "slow");
            clock.now += 500ms;
        }
    }
    CHECK(out.str()
          == "[00:00:00.000] begin Step\n"
             "[00:00:00.000] Step: planning\n"
             "[00:00:01.500] begin Step > Table a: 0/10\n"
             "[00:00:01.500] Step > Table a: 5/10 (50.0%) alter 1 column(s)\n"
             "[00:00:01.500] warning Step > Table a: slow\n"
             "[00:00:02.000] done Step > Table a (00:00:00.500)\n"
             "[00:00:02.000] done Step (00:00:02.000)\n");
}

TEST_CASE("TaskProgressRenderer ends a failed task as failed in logline style", "[dbtool][TaskProgressRenderer]")
{
    auto clock = ManualClock {};
    auto out = std::ostringstream {};
    {
        auto renderer =
            TaskProgressRenderer { TaskProgressStyle::LogLine, out, ProgressDestination::NotTerminal, clock.Source() };
        auto const task = renderer.BeginSubTask("Step", 0);
        clock.now += 250ms;
        task->Fail("boom");
    }
    CHECK(out.str()
          == "[00:00:00.000] begin Step\n"
             "[00:00:00.250] failed Step (00:00:00.250): boom\n");
}

TEST_CASE("TaskProgressRenderer keeps a failed sub-task's row with an x and the reason", "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::Ascii, out, ProgressDestination::NotTerminal };
    auto const parent = renderer.BeginSubTask("Parent", 0);
    parent->BeginSubTask("Good", 0).reset();
    {
        auto const child = parent->BeginSubTask("Bad", 0);
        child->Fail("disk full");
    }
    out.str({});
    renderer.Repaint();
    auto const frame = out.str();
    auto const badPosition = frame.find("Bad");
    REQUIRE(badPosition != std::string::npos);
    auto const rowStart = frame.find_last_of("\r\n", badPosition);
    auto row = frame.substr(rowStart + 1);
    constexpr auto clearToEndOfLine = std::string_view { "\x1b[K" };
    if (row.starts_with(clearToEndOfLine))
        row.erase(0, clearToEndOfLine.size());
    CHECK(row.starts_with("x "));
    CHECK(frame.contains("disk full"));
    CHECK_FALSE(frame.contains("Good"));
}

TEST_CASE("TaskProgressRenderer draws nothing for a failed task in style None", "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    {
        auto renderer = TaskProgressRenderer { TaskProgressStyle::None, out, ProgressDestination::NotTerminal };
        auto const task = renderer.BeginSubTask("Step", 0);
        task->Fail("boom");
        renderer.Fail("root");
    }
    CHECK(out.str().empty());
}

TEST_CASE("TaskProgressRenderer draws the unicode failure icon", "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::Unicode, out, ProgressDestination::NotTerminal };
    auto const parent = renderer.BeginSubTask("Parent", 0);
    {
        auto const child = parent->BeginSubTask("Bad", 0);
        child->Fail("x");
    }
    out.str({});
    renderer.Repaint();
    CHECK(out.str().contains("\xE2\x9D\x8C"));
}

TEST_CASE("TaskProgressRenderer draws nothing in style None", "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    {
        auto renderer = TaskProgressRenderer { TaskProgressStyle::None, out, ProgressDestination::NotTerminal };
        auto const task = renderer.BeginSubTask("Step", 3);
        task->Advance(1, "x");
        renderer.Log(ProgressTask::LogLevel::Info, "hello");
    }
    CHECK(out.str().empty());
}

TEST_CASE("TaskProgressRenderer draws a bar for a known total and a spinner without one", "[dbtool][TaskProgressRenderer]")
{
    auto clock = ManualClock {};
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::Ascii, out, ProgressDestination::NotTerminal, clock.Source() };
    auto const copy = renderer.BeginSubTask("Copy", 10);
    copy->Advance(5, "table a");
    auto const wait = renderer.BeginSubTask("Wait", 0);
    renderer.Repaint();
    auto const frame = out.str();
    CHECK(frame.contains("Copy"));
    CHECK(frame.contains("[==========..........]"));
    CHECK(frame.contains("50.00% (5/10) table a"));
    CHECK(frame.contains("| 00:00:00.000 Wait"));
}

TEST_CASE("TaskProgressRenderer drops a finished sub-task but keeps a finished top-level task",
          "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::Ascii, out, ProgressDestination::NotTerminal };
    auto first = renderer.BeginSubTask("First", 1);
    auto const second = renderer.BeginSubTask("Second", 0);
    first->BeginSubTask("Inner", 0).reset();
    first->Advance(1);
    first.reset();
    out.str({});
    renderer.Repaint();
    CHECK(out.str().contains("+ ")); // the finished top-level line, ascii "done" icon
    CHECK(out.str().contains("First"));
    CHECK_FALSE(out.str().contains("Inner"));
}

TEST_CASE("TaskProgressRenderer starts no ticker when the stream is not a terminal", "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::Unicode, out, ProgressDestination::NotTerminal };
    auto const task = renderer.BeginSubTask("Long ALTER", 0);
    auto const before = out.str();
    std::this_thread::sleep_for(3 * TaskProgressRenderer::RepaintInterval);
    CHECK(out.str() == before);
}

TEST_CASE("TaskProgressRenderer repaints on a terminal without events", "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::Ascii, out, ProgressDestination::Terminal };
    auto const task = renderer.BeginSubTask("Long ALTER", 0);
    auto const before = renderer.FramesWritten();
    auto const deadline = std::chrono::steady_clock::now() + 5s;
    while (renderer.FramesWritten() == before && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(TaskProgressRenderer::RepaintInterval);
    CHECK(renderer.FramesWritten() > before);
}

TEST_CASE("TaskProgressRenderer draws the unicode bar with FiraCode's glyphs", "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::Unicode, out, ProgressDestination::NotTerminal };
    auto const task = renderer.BeginSubTask("Copy", 20);
    task->Advance(10);
    auto const expectedBar = std::string { "\uEE03"
                                           "\uEE04\uEE04\uEE04\uEE04\uEE04\uEE04\uEE04\uEE04\uEE04\uEE04"
                                           "\uEE01\uEE01\uEE01\uEE01\uEE01\uEE01\uEE01\uEE01\uEE01\uEE01"
                                           "\uEE02 " };
    CHECK(out.str().contains(expectedBar + "50.00% (10/20)"));
}

TEST_CASE("TaskProgressRenderer prints nothing for an Advance without count or detail in logline style",
          "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::LogLine, out, ProgressDestination::NotTerminal };
    auto const task = renderer.BeginSubTask("Step", 10);
    out.str({});
    task->Advance(0, {});
    CHECK(out.str().empty());
}

TEST_CASE("TaskProgressRenderer clamps the done count to a lowered total", "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::Ascii, out, ProgressDestination::NotTerminal };
    auto const task = renderer.BeginSubTask("Step", 10);
    task->Advance(8);
    task->SetTotal(5);
    out.str({});
    renderer.Repaint();
    CHECK(out.str().contains("100.00% (5/5)"));
    CHECK_FALSE(out.str().contains("(8/5)"));
}

TEST_CASE("TaskProgressRenderer paints a later task below a final block", "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::Ascii, out, ProgressDestination::NotTerminal };
    renderer.BeginSubTask("First", 0).reset();
    out.str({});
    auto const second = renderer.BeginSubTask("Second", 0);
    CHECK(out.str().contains("Second"));
    CHECK_FALSE(out.str().contains("\033[1A"));
}

TEST_CASE("TaskProgressRenderer prints a log message above the block and repaints it", "[dbtool][TaskProgressRenderer]")
{
    auto out = std::ostringstream {};
    auto renderer = TaskProgressRenderer { TaskProgressStyle::Ascii, out, ProgressDestination::NotTerminal };
    auto const task = renderer.BeginSubTask("Step", 0);
    out.str({});
    task->Log(ProgressTask::LogLevel::Warning, "careful");
    auto const text = out.str();
    auto const moveUp = text.find("\033[1A");
    auto const message = text.find("warning careful\n");
    auto const block = text.find("Step", message);
    CHECK(moveUp != std::string::npos);
    CHECK(moveUp < message);
    CHECK(message < block);
}
