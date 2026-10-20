// SPDX-License-Identifier: Apache-2.0
//
// A quit request takes the guard against closing during a run: refused, it closes no window at all.

#include "../CloseGuard.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QtGui/QWindow>

using DbtoolGui::QuitRequestOutcome;
using DbtoolGui::RequestQuit;

TEST_CASE("a quit request during a run closes no window and says why", "[dbtool-gui][close-guard]")
{
    auto main = QWindow {};
    auto other = QWindow {};
    main.show();
    other.show();
    auto refusals = 0;

    CHECK(RequestQuit([] { return true; }, [&refusals] { ++refusals; }) == QuitRequestOutcome::Refused);
    CHECK(refusals == 1);
    CHECK(main.isVisible());
    CHECK(other.isVisible());

    CHECK(RequestQuit([] { return false; }, [&refusals] { ++refusals; }) == QuitRequestOutcome::WindowsClosed);
    CHECK(refusals == 1);
    CHECK_FALSE(main.isVisible());
    CHECK_FALSE(other.isVisible());
}
