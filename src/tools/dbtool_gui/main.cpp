// SPDX-License-Identifier: Apache-2.0
#include "Backend.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#if defined(_WIN32) || defined(_WIN64)
    #include <cstdio>
    #include <windows.h>
#endif

static void attachConsole()
{
#if defined(_WIN32) || defined(_WIN64)
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        freopen_s(reinterpret_cast<FILE**>(stdout), "CONOUT$", "w", stdout);
        freopen_s(reinterpret_cast<FILE**>(stderr), "CONOUT$", "w", stderr);
    }
#endif
}

int main(int argc, char* argv[])
{
    attachConsole();
    QGuiApplication app(argc, argv);
    app.setApplicationName("dbtool-gui");
    app.setOrganizationName("Lightweight");
    QQuickStyle::setStyle("Material");

    Backend backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/dbtool-gui/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return QGuiApplication::exec();
}
