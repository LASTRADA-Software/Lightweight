// SPDX-License-Identifier: Apache-2.0
//
// Manual triggers for the error-dialog suppression `tests/main.cpp` installs. Hidden: each one
// deliberately ends the process, so run one at a time by name. Each must print its report on
// stderr and exit non-zero without opening a dialog.

#include <catch2/catch_test_macros.hpp>

#include <QtCore/QtGlobal>

#if defined(_WIN32)
    #include <crtdbg.h>
#endif

TEST_CASE("error dialogs: qFatal goes to stderr", "[.][NoErrorDialogs]")
{
    qFatal("deliberate qFatal from the NoErrorDialogs trigger");
}

TEST_CASE("error dialogs: a failed CRT assert in a Qt test goes to stderr", "[.][NoErrorDialogs]")
{
#if defined(_WIN32) && defined(_DEBUG)
    _ASSERTE(false && "deliberate assert from the NoErrorDialogs trigger");
#else
    qFatal("deliberate qFatal from the NoErrorDialogs trigger (no CRT debug asserts here)");
#endif
}
