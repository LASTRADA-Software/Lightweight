// SPDX-License-Identifier: Apache-2.0
//
// dbtool-gui — Qt 6 GUI companion to the dbtool CLI.
//
// Exposes the same migration / SQL-query / backup / profile workflows as the
// dbtool CLI through a QML desktop UI. See docs/migrations-gui-plan.md for
// the original design notes.

#include "ThemeController.hpp"

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDebug>
#include <QtCore/QLoggingCategory>
#include <QtCore/QSettings>
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuickControls2/QQuickStyle>

#include <cstdio>
#include <cstdlib>

namespace
{

/// Routes Qt's log messages to stderr so launching the binary from a console
/// shows QML load failures and QObject warnings. Without this the WIN32
/// subsystem build drops everything on the floor, making every startup
/// problem look like "the app exited silently".
void LwQtMessageHandler(QtMsgType type, QMessageLogContext const& ctx, QString const& msg)
{
    char const* prefix = nullptr;
    switch (type)
    {
        case QtDebugMsg:
            prefix = "DEBUG";
            break;
        case QtInfoMsg:
            prefix = "INFO";
            break;
        case QtWarningMsg:
            prefix = "WARN";
            break;
        case QtCriticalMsg:
            prefix = "ERROR";
            break;
        case QtFatalMsg:
            prefix = "FATAL";
            break;
    }
    auto const file = ctx.file ? ctx.file : "";
    auto const line = ctx.line;
    std::fprintf(stderr, "[%s] %s (%s:%d)\n", prefix, msg.toLocal8Bit().constData(), file, line);
    std::fflush(stderr);
    if (type == QtFatalMsg)
        std::abort();
}

/// Settings key for the persisted theme mode. Values: "dark", "light",
/// "system". `system` defers to the platform (Windows personalization,
/// macOS appearance, `gtk-application-prefer-dark-theme`, …) and switches
/// live when the OS toggles.
constexpr auto kKeyTheme = "ui/theme";

/// Resolves the effective theme mode from (1) the command-line override if
/// provided or (2) the persisted setting, falling back to `"system"`. When a
/// CLI override is given it is also written back to settings so the next
/// launch without flags keeps the user's last choice.
QString ResolveThemeMode(QCommandLineParser const& parser,
                         QCommandLineOption const& themeOpt,
                         QSettings& settings)
{
    if (parser.isSet(themeOpt))
    {
        auto const normalized = parser.value(themeOpt).trimmed().toLower();
        if (normalized == QStringLiteral("dark") || normalized == QStringLiteral("light")
            || normalized == QStringLiteral("system"))
        {
            settings.setValue(kKeyTheme, normalized);
            return normalized;
        }
        std::fprintf(stderr,
                     "[WARN] Ignoring invalid --theme value '%s' (expected: dark, light, system)\n",
                     normalized.toLocal8Bit().constData());
        std::fflush(stderr);
    }
    return settings.value(kKeyTheme, QStringLiteral("system")).toString();
}

/// Maps a theme-mode string to the corresponding `Qt::ColorScheme` enum.
/// `Qt::ColorScheme::Unknown` is Qt's sentinel for "follow the platform",
/// which is exactly the "system" behaviour.
Qt::ColorScheme ToColorScheme(QString const& mode)
{
    if (mode == QStringLiteral("dark"))
        return Qt::ColorScheme::Dark;
    if (mode == QStringLiteral("light"))
        return Qt::ColorScheme::Light;
    return Qt::ColorScheme::Unknown;
}

} // namespace

int main(int argc, char* argv[])
{
    qInstallMessageHandler(LwQtMessageHandler);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Lightweight Migrations"));
    QGuiApplication::setOrganizationName(QStringLiteral("LASTRADA Software"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("lastrada.software"));

    std::fprintf(stderr, "[INFO] dbtool-gui starting (Qt %s)\n", qVersion());
    std::fflush(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lightweight SQL migrations GUI."));
    parser.addHelpOption();
    QCommandLineOption const themeOpt(
        { QStringLiteral("t"), QStringLiteral("theme") },
        QStringLiteral("Color theme: dark, light, or system (follow the OS). Persisted across runs."),
        QStringLiteral("mode"));
    parser.addOption(themeOpt);
    parser.process(app);

    // QSettings requires the organization/application names set above; the
    // stored value survives across runs and `--theme` overrides it on demand.
    QSettings settings;
    auto const themeMode = ResolveThemeMode(parser, themeOpt, settings);
    QGuiApplication::styleHints()->setColorScheme(ToColorScheme(themeMode));

    // Seed the QML-facing ThemeController *before* loading the QML engine
    // so the very first palette evaluation already sees the chosen mode.
    // This is the authoritative source of light/dark for QML — the
    // `setColorScheme` hint above is best-effort for the widget layer and
    // is silently ignored by the KDE Plasma platform theme plugin.
    auto const themeModeEnum = DbtoolGui::ThemeController::ModeFromString(themeMode);
    DbtoolGui::ThemeController::SeedInitialMode(themeModeEnum);
    auto const effectiveDark =
        themeModeEnum == DbtoolGui::ThemeController::Mode::Dark
        || (themeModeEnum == DbtoolGui::ThemeController::Mode::System
            && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    std::fprintf(stderr,
                 "[INFO] Theme: requested=%s, effective=%s\n",
                 themeMode.toLocal8Bit().constData(),
                 effectiveDark ? "dark" : "light");
    std::fflush(stderr);

    // "Fusion" gives us a consistent look across platforms until we commit to
    // a native style per OS. The mockup in docs/migrations-gui-mockup.html is
    // drawn in a Fluent-ish flavour that the default Fusion theme approximates
    // closely enough for prototype work.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [](QUrl const& url) {
            std::fprintf(stderr, "[ERROR] Failed to create QML root object: %s\n",
                         url.toString().toLocal8Bit().constData());
            std::fflush(stderr);
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("Lightweight.Migrations"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
    {
        std::fprintf(stderr,
                     "[ERROR] No root objects were loaded. The QML engine failed to resolve "
                     "'Main' in module 'Lightweight.Migrations'.\n");
        std::fflush(stderr);
        return 1;
    }

    std::fprintf(stderr, "[INFO] Event loop starting\n");
    std::fflush(stderr);
    return app.exec();
}
