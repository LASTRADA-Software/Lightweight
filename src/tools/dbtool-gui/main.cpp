// SPDX-License-Identifier: Apache-2.0
//
// dbtool-gui — Qt 6 GUI companion to the dbtool CLI.
//
// Exposes the same migration / SQL-query / backup / profile workflows as the
// dbtool CLI through a QML desktop UI. See docs/migrations-gui-plan.md for
// the original design notes.

#include "AppController.hpp"
#include "CloseGuard.hpp"
#include "SettingsLocation.hpp"
#include "StderrMessageHandler.hpp"

#include <cstdio>
#include <cstdlib>

#include <NoErrorDialogs.hpp>
#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDebug>
#include <QtCore/QLoggingCategory>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QPalette>
#include <QtGui/QStyleHints>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuickControls2/QQuickStyle>

namespace
{

/// One palette role and the Lastrada token colour it takes. Mirrors the
/// values in `qml/Theme.qml`; the Fusion style draws its stock controls
/// (CheckBox, ComboBox, TextField, ScrollBar, ToolTip, menus) from the
/// application palette, so seeding it here keeps those controls on-brand
/// without restyling every instance in QML.
struct PaletteEntry
{
    QPalette::ColorRole role;
    char const* color;
};

constexpr PaletteEntry kLastradaPalette[] = {
    { QPalette::Window, "#f2f2f4" },          // clrBase
    { QPalette::WindowText, "#15171c" },      // clrOnSurface
    { QPalette::Base, "#ffffff" },            // clrCard
    { QPalette::AlternateBase, "#f7f7f8" },   // clrContainerLow
    { QPalette::Text, "#15171c" },            // clrOnSurface
    { QPalette::PlaceholderText, "#9a9fab" }, // clrOnSurfaceFaint
    { QPalette::Button, "#ffffff" },          // clrCard
    { QPalette::ButtonText, "#15171c" },      // clrOnSurface
    { QPalette::BrightText, "#ffffff" },
    { QPalette::Highlight, "#a21928" }, // clrPrimary
    { QPalette::HighlightedText, "#ffffff" },
    { QPalette::Accent, "#a21928" },      // clrPrimary
    { QPalette::Link, "#a21928" },        // clrPrimary
    { QPalette::LinkVisited, "#8a1522" }, // clrPrimaryHover
    { QPalette::ToolTipBase, "#15171c" }, // clrOnSurface (dark tooltip, as in the kit)
    { QPalette::ToolTipText, "#ffffff" },
    { QPalette::Light, "#ffffff" },
    { QPalette::Midlight, "#ececee" }, // clrContainer
    { QPalette::Mid, "#d2d3d8" },      // clrContainerHighest
    { QPalette::Dark, "#aeb2bb" },     // clrBorderStrong
    { QPalette::Shadow, "#6b717e" },   // clrOnSurfaceSubtle
};

/// Builds the light Lastrada palette used for every Fusion control.
/// Disabled text is faded to `clrOnSurfaceFaint` so disabled controls read
/// as such without per-control opacity tweaks.
QPalette LastradaPalette()
{
    QPalette palette;
    for (auto const& [role, color]: kLastradaPalette)
        palette.setColor(role, QColor(QLatin1StringView(color)));
    auto const faint = QColor(QStringLiteral("#9a9fab"));
    for (auto const role: { QPalette::WindowText, QPalette::Text, QPalette::ButtonText })
        palette.setColor(QPalette::Disabled, role, faint);
    return palette;
}

} // namespace

int main(int argc, char* argv[])
{
    // Opt-in for scripted runs (tests, CI); an interactive user may still want the debugger prompt.
    if (qEnvironmentVariable("LIGHTWEIGHT_NO_ERROR_DIALOGS") == QStringLiteral("1"))
        Lightweight::Tools::DisableErrorDialogs();
    qInstallMessageHandler(DbtoolGui::WriteQtMessageToStderr);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Lightweight Migrations"));
    QGuiApplication::setOrganizationName(QStringLiteral("JP-Software"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("lastrada.software"));

    DbtoolGui::UseSettingsDirectoryFromEnvironment();

    // The "dbtool-gui starting (Qt …)" / "Theme: …" / "Event loop starting"
    // lines now appear in the GUI's log pane (emitted by `AppController`'s
    // constructor / startup banner). Keeping stderr copies as well would
    // double-up the developer-facing console and the user-facing pane.

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lightweight SQL migrations GUI."));
    parser.addHelpOption();
    QCommandLineOption const verboseOpt(
        { QStringLiteral("v"), QStringLiteral("verbose") },
        QStringLiteral("Emit informational messages (e.g. shadowed plugins) via qInfo() to stderr."));
    parser.addOption(verboseOpt);
    parser.process(app);

    DbtoolGui::AppController::SeedVerbose(parser.isSet(verboseOpt));

    // The GUI follows the light-only Lastrada UI design system. Pin the
    // platform colour scheme to light so a dark OS setting cannot flip the
    // Fusion controls' palette underneath the QML `Theme` tokens.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);

    // "Fusion" draws every stock control from the application palette, which
    // makes it the one style we can fully re-colour: the palette and base font
    // below carry the Lastrada tokens (brand red highlight, Segoe UI 13 px).
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QGuiApplication::setPalette(LastradaPalette());
    auto font = QGuiApplication::font();
    font.setFamilies({ QStringLiteral("Segoe UI"), QStringLiteral("system-ui") });
    font.setPixelSize(13);
    QGuiApplication::setFont(font);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [](QUrl const& url) {
            std::fprintf(stderr, "[ERROR] Failed to create QML root object: %s\n", url.toString().toLocal8Bit().constData());
            std::fflush(stderr);
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    // Qt.quit() takes the guard against closing during a run instead of quitting at once.
    DbtoolGui::InstallQuitGuard(engine);

    engine.loadFromModule(QStringLiteral("Lightweight.Migrations"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
    {
        std::fprintf(stderr,
                     "[ERROR] No root objects were loaded. The QML engine failed to resolve "
                     "'Main' in module 'Lightweight.Migrations'.\n");
        std::fflush(stderr);
        return 1;
    }

    return app.exec();
}
