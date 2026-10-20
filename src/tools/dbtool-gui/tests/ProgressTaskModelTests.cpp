// SPDX-License-Identifier: Apache-2.0

#include "../Models/ProgressTaskModel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <vector>

#include <QtCore/QCoreApplication>

using namespace std::chrono_literals;
using DbtoolGui::ProgressTaskModel;
using Lightweight::ProgressTask;

namespace
{
/// Delivers the queued calls the tasks posted.
void ProcessEvents()
{
    QCoreApplication::processEvents();
}

QVariant Data(ProgressTaskModel const& model, int row, ProgressTaskModel::Role role)
{
    return model.data(model.index(row, 0), role);
}
} // namespace

TEST_CASE("ProgressTaskModel lists sub-tasks in tree order with their depth", "[dbtool-gui][ProgressTaskModel]")
{
    auto model = ProgressTaskModel {};
    auto root = model.CreateRootTask({}, {});
    auto step = root->BeginSubTask("Step", 0);
    auto table = step->BeginSubTask("Table", 10);
    auto migrations = root->BeginSubTask("Migrations", 3);
    ProcessEvents();
    REQUIRE(model.rowCount() == 3);
    CHECK(Data(model, 0, ProgressTaskModel::TitleRole).toString() == "Step");
    CHECK(Data(model, 1, ProgressTaskModel::TitleRole).toString() == "Table");
    CHECK(Data(model, 1, ProgressTaskModel::DepthRole).toInt() == 1);
    CHECK(Data(model, 2, ProgressTaskModel::TitleRole).toString() == "Migrations");
    CHECK(Data(model, 0, ProgressTaskModel::IndeterminateRole).toBool());
    CHECK_FALSE(Data(model, 1, ProgressTaskModel::IndeterminateRole).toBool());
    CHECK(model.active());
}

TEST_CASE("ProgressTaskModel applies the latest state of a task, at most once per interval",
          "[dbtool-gui][ProgressTaskModel]")
{
    auto model = ProgressTaskModel {};
    auto root = model.CreateRootTask({}, {});
    auto task = root->BeginSubTask("Copy", 100);
    ProcessEvents();
    task->Advance(10, "a");
    task->Advance(20, "b");
    task->Advance(0, "c");
    model.pollNow();
    CHECK(Data(model, 0, ProgressTaskModel::DoneRole).toULongLong() == 30);
    CHECK(Data(model, 0, ProgressTaskModel::DetailRole).toString() == "c");
}

TEST_CASE("ProgressTaskModel removes a finished sub-task and keeps a finished top-level task until cleared",
          "[dbtool-gui][ProgressTaskModel]")
{
    auto model = ProgressTaskModel {};
    auto root = model.CreateRootTask({}, {});
    auto step = root->BeginSubTask("Step", 0);
    step->BeginSubTask("Inner", 0).reset();
    step.reset();
    ProcessEvents();
    REQUIRE(model.rowCount() == 1);
    CHECK(Data(model, 0, ProgressTaskModel::FinishedRole).toBool());
    CHECK_FALSE(model.active());
    model.clear();
    CHECK(model.rowCount() == 0);
}

TEST_CASE("ProgressTaskModel keeps a failed sub-task, with its reason, and the rows above it",
          "[dbtool-gui][ProgressTaskModel]")
{
    auto model = ProgressTaskModel {};
    auto root = model.CreateRootTask({}, {});
    auto step = root->BeginSubTask("Step", 0);
    auto unit = step->BeginSubTask("Unit", 0);
    auto table = unit->BeginSubTask("Table", 0);
    table->Fail("disk full");
    table.reset();
    unit.reset();
    step.reset();
    ProcessEvents();
    REQUIRE(model.rowCount() == 3);
    CHECK(Data(model, 1, ProgressTaskModel::TitleRole).toString() == "Unit");
    CHECK_FALSE(Data(model, 1, ProgressTaskModel::FailedRole).toBool());
    CHECK(Data(model, 2, ProgressTaskModel::TitleRole).toString() == "Table");
    CHECK(Data(model, 2, ProgressTaskModel::FailedRole).toBool());
    CHECK(Data(model, 2, ProgressTaskModel::FinishedRole).toBool());
    CHECK(Data(model, 2, ProgressTaskModel::FailureReasonRole).toString() == "disk full");
    CHECK_FALSE(model.active());
}

TEST_CASE("ProgressTaskModel measures elapsed time with its clock", "[dbtool-gui][ProgressTaskModel]")
{
    auto now = std::chrono::steady_clock::time_point {} + 1h;
    auto model = ProgressTaskModel { nullptr, [&now] { return now; } };
    auto root = model.CreateRootTask({}, {});
    auto task = root->BeginSubTask("Long ALTER", 0);
    ProcessEvents();
    now += 2500ms;
    CHECK(Data(model, 0, ProgressTaskModel::ElapsedMsRole).toLongLong() == 2500);
    task.reset();
    ProcessEvents();
    now += 1s;
    CHECK(Data(model, 0, ProgressTaskModel::ElapsedMsRole).toLongLong() == 2500); // frozen once finished
}

TEST_CASE("ProgressTaskModel's tasks report cancellation and logs through the run's callbacks",
          "[dbtool-gui][ProgressTaskModel]")
{
    auto model = ProgressTaskModel {};
    auto cancelled = false;
    auto logged = QStringList {};
    auto root = model.CreateRootTask([&cancelled] { return cancelled; },
                                     [&logged](ProgressTask::LogLevel, QString const& message) { logged.append(message); });
    auto task = root->BeginSubTask("Step", 0);
    CHECK_FALSE(task->IsCancellationRequested());
    cancelled = true;
    CHECK(task->IsCancellationRequested());
    task->Log(ProgressTask::LogLevel::Warning, "careful");
    CHECK(logged == QStringList { "careful" });
}

TEST_CASE("ProgressTaskModel logs a failed task with its title and reason at error level", "[dbtool-gui][ProgressTaskModel]")
{
    auto model = ProgressTaskModel {};
    auto logged = QStringList {};
    auto levels = std::vector<ProgressTask::LogLevel> {};
    auto root = model.CreateRootTask({}, [&](ProgressTask::LogLevel level, QString const& message) {
        levels.push_back(level);
        logged.append(message);
    });
    auto task = root->BeginSubTask("Widen text columns", 0);
    task->Fail("disk full");
    CHECK(logged == QStringList { "failed Widen text columns: disk full" });
    CHECK(levels == std::vector { ProgressTask::LogLevel::Error });
}

TEST_CASE("ProgressTaskModel accepts tasks driven from another thread", "[dbtool-gui][ProgressTaskModel]")
{
    auto model = ProgressTaskModel {};
    auto root = model.CreateRootTask({}, {});
    std::thread { [&root] {
        auto task = root->BeginSubTask("Worker", 4);
        task->Advance(4, "done");
    } }.join();
    ProcessEvents();
    model.pollNow();
    REQUIRE(model.rowCount() == 1);
    CHECK(Data(model, 0, ProgressTaskModel::FinishedRole).toBool());
    CHECK(Data(model, 0, ProgressTaskModel::DoneRole).toULongLong() == 4);
}

TEST_CASE("ProgressTaskModel marks a task that stopped short of its total during a cancel as cancelled, not one that ended "
          "normally",
          "[dbtool-gui][ProgressTaskModel]")
{
    auto model = ProgressTaskModel {};
    auto cancelled = false;
    auto root = model.CreateRootTask([&cancelled] { return cancelled; }, {});
    auto completed = root->BeginSubTask("Completed", 2);
    completed->Advance(2);
    completed.reset();
    auto interrupted = root->BeginSubTask("Interrupted", 3);
    interrupted->Advance(1);
    auto sweeping = root->BeginSubTask("Sweeping", 0);
    cancelled = true;
    interrupted.reset();
    sweeping.reset();
    ProcessEvents();
    REQUIRE(model.rowCount() == 3);
    CHECK_FALSE(Data(model, 0, ProgressTaskModel::CancelledRole).toBool());
    CHECK(Data(model, 1, ProgressTaskModel::CancelledRole).toBool());
    CHECK_FALSE(Data(model, 2, ProgressTaskModel::CancelledRole).toBool());
    CHECK(Data(model, 1, ProgressTaskModel::FinishedRole).toBool());
}
