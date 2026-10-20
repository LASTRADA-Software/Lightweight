// SPDX-License-Identifier: Apache-2.0
//
// The UI watchdog the workflow tests run: it measures a GUI-thread stall and stays under budget
// while the event loop runs freely.

#include "support/UiWatchdog.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include <QtTest/QTest>

using namespace std::chrono_literals;
using DbtoolGui::Tests::UiWatchdog;

TEST_CASE("UiWatchdog records a GUI-thread stall longer than its budget", "[dbtool-gui][watchdog]")
{
    UiWatchdog watchdog;
    watchdog.armNow();
    QTest::qWait(100);
    std::this_thread::sleep_for(1200ms);
    QTest::qWait(50);
    watchdog.stop();

    CHECK(watchdog.maxGapMs() >= 1200);
    CHECK(watchdog.exceeded());
}

TEST_CASE("UiWatchdog measures the gap between heartbeats, not the time since arming", "[dbtool-gui][watchdog]")
{
    UiWatchdog watchdog;
    watchdog.setBudgetMs(200);
    watchdog.armNow();
    QTest::qWait(600);
    watchdog.stop();

    CHECK(watchdog.maxGapMs() < 200);
    CHECK_FALSE(watchdog.exceeded());
}

TEST_CASE("UiWatchdog stays under budget while the event loop runs freely", "[dbtool-gui][watchdog]")
{
    UiWatchdog watchdog;
    watchdog.armNow();
    QTest::qWait(300);
    watchdog.stop();

    CHECK(watchdog.maxGapMs() < 1000);
    CHECK_FALSE(watchdog.exceeded());
}

TEST_CASE("UiWatchdog names the step of its longest gap", "[dbtool-gui][watchdog]")
{
    UiWatchdog watchdog;
    watchdog.setStep(QStringLiteral("idle"));
    watchdog.armNow();
    QTest::qWait(50);
    watchdog.setStep(QStringLiteral("stall inside"));
    QTest::qWait(50);
    std::this_thread::sleep_for(300ms);
    QTest::qWait(50);
    CHECK(watchdog.maxGapStep() == QStringLiteral("stall inside"));

    // A gap that begins in one step and ends in the next names both.
    watchdog.setStep(QStringLiteral("before"));
    QTest::qWait(50);
    watchdog.setStep(QStringLiteral("after"));
    std::this_thread::sleep_for(600ms);
    QTest::qWait(50);
    watchdog.stop();
    CHECK(watchdog.maxGapStep() == QStringLiteral("before → after"));
}

TEST_CASE("UiWatchdog does not count a stall while paused", "[dbtool-gui][watchdog]")
{
    UiWatchdog watchdog;
    watchdog.setBudgetMs(200);
    watchdog.armNow();
    QTest::qWait(50);
    watchdog.pause();
    std::this_thread::sleep_for(400ms);
    watchdog.resume();
    QTest::qWait(50);
    watchdog.stop();

    CHECK(watchdog.maxGapMs() < 200);
    CHECK_FALSE(watchdog.exceeded());
}
