// SPDX-License-Identifier: Apache-2.0
//
// Entry point of the dbtool-gui workflow tests: Qt Quick Test with the `DbtoolGuiWorkflow` module
// (`WorkflowFixture`, `UiWatchdog`) registered into its engine. CTest runs one test function per
// process, so every test gets a fresh `AppController`. See `tests/qmltest_main.cpp` for the shared set-up.

#include "../../AppController.hpp"
#include "../../StderrMessageHandler.hpp"
#include "../TestIsolation.hpp"
#include "../support/WorkflowFixture.hpp"
#include "../support/WorkflowSetup.hpp"

#include <cstdio>

#include <NoErrorDialogs.hpp>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
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
    qInstallMessageHandler(DbtoolGui::Tests::RecordQmlWarningsAndWriteToStderr);
    DbtoolGui::AppController::SeedUrlOpener(&DbtoolGui::Tests::RecordOpenedUrl);
    QTEST_SET_MAIN_SOURCE_PATH
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
#ifdef _WIN32
    // Without a console QtTest logs to the debugger, not to the stdout CTest captures.
    if (!qEnvironmentVariableIsSet("QT_FORCE_STDERR_LOGGING"))
        qputenv("QT_FORCE_STDERR_LOGGING", "1");
#endif
    // QML `Settings` refuses to work without these; the values only name the isolated settings store.
    QCoreApplication::setApplicationName(QStringLiteral("dbtool-gui-workflowtest"));
    QCoreApplication::setOrganizationName(QStringLiteral("Lightweight"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("lightweight.example"));
    auto result = 0;
    {
        DbtoolGui::Tests::ScopedTestIsolation const isolation;
#ifndef _WIN32
        // unixODBC reads user DSNs from $ODBCINI: keep the user's own file out of every enumeration.
        QTemporaryDir const odbcDirectory;
        auto const odbcIni = odbcDirectory.filePath(QStringLiteral("odbc.ini"));
        QFile { odbcIni }.open(QIODevice::WriteOnly);
        qputenv("ODBCINI", QFile::encodeName(odbcIni));
#endif
        DbtoolGui::Tests::WorkflowSetup setup;
        result = quick_test_main_with_setup(argc, argv, "dbtool_gui_workflowtest", nullptr, &setup);
    } // leaves and removes the isolation directory, which TerminateProcess below would skip
    std::fflush(nullptr); // TerminateProcess skips the CRT's flush of buffered streams
#ifdef _WIN32
    // See `tests/main.cpp`: Qt 6 debug builds on Windows crash in DLL_PROCESS_DETACH after a QML session.
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(result));
#endif
    return result;
}
