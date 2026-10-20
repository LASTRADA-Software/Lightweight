// SPDX-License-Identifier: Apache-2.0
//
// Helpers for the Catch2 tests that drive an `AppController`, whose plugin loads and ODBC refreshes
// run on workers and land through the event loop.

#pragma once

#include "../AppController.hpp"

#include <algorithm>
#include <chrono>

#include <QtCore/QCoreApplication>
#include <QtCore/QDeadlineTimer>
#include <QtTest/QSignalSpy>

namespace DbtoolGui::Tests
{

/// Runs the event loop until no controller job is queued or running.
/// @param controller The controller to wait for.
/// @param timeout How long to wait at most.
/// @return Whether the controller became idle in time.
[[nodiscard]] inline bool WaitForIdle(DbtoolGui::AppController& controller,
                                      std::chrono::milliseconds timeout = std::chrono::seconds { 30 })
{
    auto const deadline = QDeadlineTimer { timeout };
    while (controller.busy())
    {
        if (deadline.hasExpired())
            return false;
        auto activity = QSignalSpy { &controller, &DbtoolGui::AppController::activityChanged };
        if (controller.busy())
            (void) activity.wait(static_cast<int>(std::clamp<qint64>(deadline.remainingTime(), 1, 100)));
    }
    // Deliver what the last job posted besides its outcome, such as its log lines.
    QCoreApplication::processEvents();
    return true;
}

} // namespace DbtoolGui::Tests
