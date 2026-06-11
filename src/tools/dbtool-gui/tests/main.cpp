// SPDX-License-Identifier: Apache-2.0
//
// Custom Catch2 entry point that boots a single `QGuiApplication` before
// any test runs. `AppController` and its child QObjects (MigrationRunner,
// list models) assume a Qt event loop is reachable for queued signals,
// `QThreadPool`, and Qt::QueuedConnection delivery — without `QGuiApplication`
// even constructing them is unsafe.

#define CATCH_CONFIG_RUNNER

#include <catch2/catch_session.hpp>

#include <cstdlib>

#include <QtCore/QCoreApplication>
#include <QtCore/QThreadPool>
#include <QtGui/QGuiApplication>

#ifdef _WIN32
    #include <windows.h>
#endif

int main(int argc, char* argv[])
{
    // Heap-allocated and intentionally never deleted. Each test case
    // constructs an `AppController` that owns a `QThreadPool`-driven
    // `MigrationRunner` and a `QFileSystemWatcher`; both can have queued
    // events in flight when the test cleans up its local controller.
    // Letting the OS reap the QGuiApplication on process exit sidesteps
    // the access violations a stack-allocated app raises during static
    // tear-down on Windows + Qt 6.11 debug builds.
    auto* app = new QGuiApplication(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("dbtool-gui-tests"));
    QGuiApplication::setOrganizationName(QStringLiteral("JP-Software"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("lastrada.software"));

    Catch::Session session;
    auto const result = session.run(argc, argv);
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    (void) app;

    // Bypass C++ static destructors. Qt 6.11 debug builds + Catch2's own
    // static fixtures + the Lightweight library's static caches don't tear
    // down in a defined order on Windows, and the result is an access
    // violation *after* every test has already passed. The OS reclaims
    // every resource we hold; bypassing C++ tear-down skips the corruption
    // path. `ExitProcess` is used on Windows because MSVC's `std::_Exit`
    // still segfaults through some Qt/CRT cleanup path on debug builds.
#ifdef _WIN32
    // TerminateProcess is harsher than ExitProcess: it skips DLL_PROCESS_
    // DETACH callbacks entirely. On Qt 6 debug builds (Windows + clang-cl)
    // those callbacks segfault when QML resources, QtConcurrent worker
    // threads, and AppController QObject lifetimes interact during
    // shutdown — and we have already collected the test result here so
    // there is nothing left to clean up gracefully.
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(result));
#endif
    return result;
}
