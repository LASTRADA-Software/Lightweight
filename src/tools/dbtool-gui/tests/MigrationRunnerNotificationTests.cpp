// SPDX-License-Identifier: Apache-2.0
//
// A run's notifications reach the GUI thread in timed batches: however fast the worker queues them,
// one batch spends at most `MigrationRunner::DrainBudget`, so the event loop keeps running in between.

#include "../MigrationRunner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <thread>

#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

using DbtoolGui::LogLevel;
using DbtoolGui::MigrationRunner;

TEST_CASE("a flood of run notifications arrives in order without stalling the event loop",
          "[dbtool-gui][MigrationRunner][notifications]")
{
    constexpr auto lineCount = 40000;
    // Each line costs the receiver this much, so draining all lines at once would stall for a second.
    constexpr auto costPerLine = std::chrono::microseconds { 25 };

    auto runner = MigrationRunner {};
    auto received = 0;
    auto inOrder = true;
    QObject::connect(&runner, &MigrationRunner::logLine, [&](QString const& line, LogLevel /*level*/) {
        inOrder = inOrder && line == QString::number(received);
        ++received;
        auto clock = QElapsedTimer {};
        clock.start();
        while (clock.durationElapsed() < costPerLine)
        {
        }
    });
    auto linesAtFinish = -1;
    QObject::connect(
        &runner, &MigrationRunner::finished, [&](bool /*ok*/, QString const& /*summary*/) { linesAtFinish = received; });

    // The heartbeat measures the longest time the event loop spent away from its timers.
    auto heartbeat = QTimer {};
    heartbeat.setInterval(std::chrono::milliseconds { 2 });
    auto sinceBeat = QElapsedTimer {};
    auto longestGap = std::chrono::nanoseconds::zero();
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
        if (!sinceBeat.isValid())
            return sinceBeat.start();
        longestGap = std::max(longestGap, sinceBeat.durationElapsed());
        sinceBeat.restart();
    });
    heartbeat.start();

    auto finished = QSignalSpy { &runner, &MigrationRunner::finished };
    auto worker = std::thread { [&runner, lineCount] {
        auto notification = MigrationRunner::Notification {};
        for (auto const index: std::views::iota(0, lineCount))
        {
            notification.text = QString::number(index);
            runner.Notify(notification);
        }
        notification.kind = MigrationRunner::Notification::Kind::Finished;
        notification.text = QStringLiteral("done");
        notification.ok = true;
        runner.Notify(notification);
    } };
    worker.join();

    REQUIRE(finished.wait(30000));
    // A few more beats, so the gap that ends with the last batch is measured too.
    QTest::qWait(20);
    CHECK(received == lineCount);
    CHECK(inOrder);
    CHECK(linesAtFinish == lineCount);
    CHECK(runner.phase() == MigrationRunner::Phase::Idle);
    // One batch is DrainBudget plus one line; allow generously for a loaded machine.
    CHECK(longestGap < std::chrono::milliseconds { 250 });
}
