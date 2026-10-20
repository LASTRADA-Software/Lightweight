// SPDX-License-Identifier: Apache-2.0
//
// Entry point for the QML test runner. Inlines what `QUICK_TEST_MAIN`
// expands to so we can wrap the result with `TerminateProcess` on Windows
// debug builds — see the matching note in `tests/main.cpp`. The QML files
// under test live wherever the `-input <dir>` argument points, supplied by
// CMake's `add_test` line so the build tree stays out of the executable.

#include "../StderrMessageHandler.hpp"
#include "TestIsolation.hpp"

#include <cstdio>

#include <NoErrorDialogs.hpp>
#include <QtCore/QtEnvironmentVariables>
#include <QtQuickControls2/QQuickStyle>
#include <QtQuickTest/quicktest.h>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

int main(int argc, char** argv)
{
    Lightweight::Tools::DisableErrorDialogs();
    qInstallMessageHandler(DbtoolGui::WriteQtMessageToStderr);
    QTEST_SET_MAIN_SOURCE_PATH
    // Match the application: `main.cpp` runs every control under Fusion, and
    // the kit components (LsButton, …) replace `background` / `contentItem`,
    // which the platform-native styles refuse to customise.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
#ifdef _WIN32
    // Without a console QtTest logs to the debugger, not to the stdout CTest captures.
    if (!qEnvironmentVariableIsSet("QT_FORCE_STDERR_LOGGING"))
        qputenv("QT_FORCE_STDERR_LOGGING", "1");
#endif
    // Source dir is supplied at runtime via `-input <dir>`; pass `nullptr`
    // here so qmltestrunner doesn't fall back to a build-time path that
    // wouldn't exist on the CI runner.
    auto result = 0;
    {
        DbtoolGui::Tests::ScopedTestIsolation const isolation;
        result = quick_test_main(argc, argv, "dbtool_gui_qmltest", nullptr);
    } // leaves and removes the isolation directory, which TerminateProcess below would skip
    std::fflush(nullptr); // TerminateProcess skips the CRT's flush of buffered streams
#ifdef _WIN32
    // See `tests/main.cpp` for the rationale: Qt 6 debug builds on
    // Windows segfault during DLL_PROCESS_DETACH after a QML test session,
    // even when every test passes. Skip the C++ tear-down phase entirely.
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(result));
#endif
    return result;
}
