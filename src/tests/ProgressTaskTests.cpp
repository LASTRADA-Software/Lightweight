// SPDX-License-Identifier: Apache-2.0

#include "RecordingProgressTask.hpp"

#include <Lightweight/ProgressTask.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using Lightweight::NullProgressTask;
using Lightweight::ProgressTask;

TEST_CASE("NullProgressTask opens sub-tasks, accepts every call and is never cancelled", "[progress]")
{
    auto root = NullProgressTask {};
    auto child = root.BeginSubTask("child", 10);
    REQUIRE(child != nullptr);
    auto grandChild = child->BeginSubTask("grand child", 0);
    REQUIRE(grandChild != nullptr);
    child->SetTotal(20);
    child->Advance(5, "half");
    child->Log(ProgressTask::LogLevel::Warning, "message");
    child->Fail("reason");
    CHECK_FALSE(child->IsCancellationRequested());
    CHECK_FALSE(root.IsCancellationRequested());
}

TEST_CASE("A task finishes when it is destroyed, after its children", "[progress]")
{
    auto journal = RecordingProgressTask::Journal {};
    {
        auto root = RecordingProgressTask { journal };
        auto parent = root.BeginSubTask("parent", 2);
        {
            auto child = parent->BeginSubTask("child", 0);
            child->Advance(0, "working");
        }
        parent->Advance(2);
    }
    CHECK(journal.events
          == std::vector<std::string> {
              "begin parent total=2",
              "begin parent/child total=0",
              "advance parent/child +0 working",
              "end parent/child",
              "advance parent +2",
              "end parent",
          });
}

TEST_CASE("RecordingProgressTask records a failure and the end of the failed task", "[progress]")
{
    auto journal = RecordingProgressTask::Journal {};
    {
        auto root = RecordingProgressTask { journal };
        auto const task = root.BeginSubTask("task", 0);
        task->Fail("boom");
    }
    CHECK(journal.events == std::vector<std::string> { "begin task total=0", "fail task boom", "end task" });
}

TEST_CASE("RecordingProgressTask reports the cancellation its journal decides", "[progress]")
{
    auto journal = RecordingProgressTask::Journal {};
    journal.cancelWhen = [](std::vector<std::string> const& events) {
        return events.size() >= 2;
    };
    auto root = RecordingProgressTask { journal };
    auto const first = root.BeginSubTask("first", 0);
    CHECK_FALSE(root.IsCancellationRequested());
    auto const second = root.BeginSubTask("second", 0);
    CHECK(root.IsCancellationRequested());
    CHECK(second->IsCancellationRequested());
}
