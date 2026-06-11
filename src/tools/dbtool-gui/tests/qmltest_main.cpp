// SPDX-License-Identifier: Apache-2.0
//
// Entry point for the QML test runner. Inlines what `QUICK_TEST_MAIN`
// expands to so we can wrap the result with `TerminateProcess` on Windows
// debug builds — see the matching note in `tests/main.cpp`. The QML files
// under test live wherever the `-input <dir>` argument points, supplied by
// CMake's `add_test` line so the build tree stays out of the executable.

#include <QtQuickTest/quicktest.h>

#ifdef _WIN32
    #include <windows.h>
#endif

int main(int argc, char** argv)
{
    QTEST_SET_MAIN_SOURCE_PATH
    // Source dir is supplied at runtime via `-input <dir>`; pass `nullptr`
    // here so qmltestrunner doesn't fall back to a build-time path that
    // wouldn't exist on the CI runner.
    auto const result = quick_test_main(argc, argv, "dbtool_gui_qmltest", nullptr);
#ifdef _WIN32
    // See `tests/main.cpp` for the rationale: Qt 6 debug builds on
    // Windows segfault during DLL_PROCESS_DETACH after a QML test session,
    // even when every test passes. Skip the C++ tear-down phase entirely.
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(result));
#endif
    return result;
}
