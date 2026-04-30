// SPDX-License-Identifier: Apache-2.0
//
// `ThemeController` owns the effective light/dark decision the QML palette
// binds against. It exists because `Qt.styleHints.colorScheme` is not a
// reliable source of truth on every desktop: on KDE Plasma the platform
// theme plugin re-applies the system colour scheme over any
// `QGuiApplication::styleHints()->setColorScheme(...)` we issue, so a
// `--theme light` override never reaches a QML palette that binds directly
// to `Qt.styleHints.colorScheme`.
//
// When the user picks an explicit mode (`light` or `dark`) we therefore
// freeze `dark` at that value and ignore `QStyleHints` entirely. In
// `system` mode we mirror `QStyleHints::colorScheme()` and re-emit
// `darkChanged` from its `colorSchemeChanged` signal so live DE toggles
// still propagate to the UI.

#pragma once

#include <QtCore/QObject>
#include <QtQml/QQmlEngine>

namespace DbtoolGui
{

class ThemeController: public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

  public:
    /// User-facing theme choice. Matches the strings accepted by the
    /// `--theme` CLI option and persisted in `QSettings`.
    enum class Mode
    {
        System, ///< Follow the platform colour scheme.
        Light,  ///< Force light regardless of platform.
        Dark,   ///< Force dark regardless of platform.
    };
    Q_ENUM(Mode)

    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)

    explicit ThemeController(QObject* parent = nullptr);
    ~ThemeController() override;

    /// Qt QML singleton factory. Returns the process-wide instance and
    /// transfers ownership to C++ (Qt's default would be JS GC).
    static ThemeController* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    /// Seeds the initial mode used when the singleton is first
    /// instantiated. Call from `main()` *before* loading the QML engine so
    /// the very first palette evaluation already sees the correct value.
    /// No-op once the singleton has been materialised.
    static void SeedInitialMode(Mode mode);

    /// Parses the theme-mode string used by `--theme` / `QSettings`.
    /// Accepts `"system"`, `"light"`, `"dark"` (case-insensitive). Any
    /// other value resolves to `Mode::System`.
    static Mode ModeFromString(QString const& text);

    /// Inverse of `ModeFromString` — returns the canonical string form.
    static QString ModeToString(Mode mode);

    [[nodiscard]] Mode mode() const noexcept
    {
        return _mode;
    }
    void setMode(Mode mode);

    /// Effective boolean the UI should paint from. In `System` mode this
    /// mirrors `QStyleHints::colorScheme() == Qt::ColorScheme::Dark`; in
    /// `Light` / `Dark` it is forced to the user's choice.
    [[nodiscard]] bool dark() const;

  signals:
    void modeChanged();
    void darkChanged();

  private:
    /// Recomputes `dark()` and emits `darkChanged` when it actually moves.
    /// Called both when the user switches mode and when the platform
    /// colour scheme changes underneath us in `System` mode.
    void Reevaluate();

    Mode _mode;
    bool _lastDark;
};

} // namespace DbtoolGui
