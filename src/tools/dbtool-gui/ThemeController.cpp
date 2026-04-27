// SPDX-License-Identifier: Apache-2.0

#include "ThemeController.hpp"

#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>

namespace DbtoolGui
{

namespace
{

// Seed + process-wide singleton storage. `AppController` follows the same
// pattern; see its `create()` for rationale — QML would otherwise instantiate
// a fresh controller per engine, and we want a single authoritative mode.
ThemeController::Mode g_seededMode = ThemeController::Mode::System;
ThemeController* g_instance = nullptr;

/// Reads Qt's current platform colour-scheme guess. Used by `dark()` when
/// the user is in `System` mode. We treat the `Unknown` enum value as
/// light, matching Qt's own "fall back to light" behaviour in widgets.
bool PlatformIsDark()
{
    auto const scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
}

} // namespace

ThemeController::ThemeController(QObject* parent):
    QObject(parent),
    _mode(g_seededMode),
    _lastDark(_mode == Mode::Dark || (_mode == Mode::System && PlatformIsDark()))
{
    // `colorSchemeChanged` fires whenever the DE toggles light/dark while
    // the app is open. We always listen — cheap — and filter in
    // `Reevaluate()` so `Light` / `Dark` modes ignore it.
    connect(QGuiApplication::styleHints(),
            &QStyleHints::colorSchemeChanged,
            this,
            [this](Qt::ColorScheme) { Reevaluate(); });
}

ThemeController::~ThemeController()
{
    if (g_instance == this)
        g_instance = nullptr;
}

ThemeController* ThemeController::create(QQmlEngine* /*engine*/, QJSEngine* /*scriptEngine*/)
{
    if (!g_instance)
        g_instance = new ThemeController();
    QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
    return g_instance;
}

void ThemeController::SeedInitialMode(Mode mode)
{
    g_seededMode = mode;
    if (g_instance)
        g_instance->setMode(mode);
}

ThemeController::Mode ThemeController::ModeFromString(QString const& text)
{
    auto const normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("dark"))
        return Mode::Dark;
    if (normalized == QStringLiteral("light"))
        return Mode::Light;
    return Mode::System;
}

QString ThemeController::ModeToString(Mode mode)
{
    switch (mode)
    {
        case Mode::Dark:
            return QStringLiteral("dark");
        case Mode::Light:
            return QStringLiteral("light");
        case Mode::System:
            return QStringLiteral("system");
    }
    return QStringLiteral("system");
}

void ThemeController::setMode(Mode mode)
{
    if (_mode == mode)
        return;
    _mode = mode;
    emit modeChanged();
    Reevaluate();
}

bool ThemeController::dark() const
{
    switch (_mode)
    {
        case Mode::Dark:
            return true;
        case Mode::Light:
            return false;
        case Mode::System:
            return PlatformIsDark();
    }
    return false;
}

void ThemeController::Reevaluate()
{
    auto const now = dark();
    if (now == _lastDark)
        return;
    _lastDark = now;
    emit darkChanged();
}

} // namespace DbtoolGui
