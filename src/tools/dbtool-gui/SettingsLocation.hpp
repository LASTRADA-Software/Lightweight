// SPDX-License-Identifier: Apache-2.0
//
// Lets a scripted dbtool-gui run (the smoke test, the GUI test executables) keep its QSettings
// out of the user's registry or settings file.

#pragma once

#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QtEnvironmentVariables>

namespace DbtoolGui
{

/// Environment variable naming the directory that holds dbtool-gui's settings as an INI file.
/// When set, the default backup folder (no folder chosen in Settings, no `defaultBackupDir`) is
/// its `backups` subdirectory too (`ManagedBackupController::defaultBackupFolder`).
inline constexpr char const* SettingsDirectoryVariable = "LIGHTWEIGHT_DBTOOL_GUI_SETTINGS_DIR";

/// When `SettingsDirectoryVariable` is set, makes every default-constructed `QSettings` (and
/// QML `Settings`) use an INI file under that directory instead of the user's settings.
/// Call it before anything reads a setting.
inline void UseSettingsDirectoryFromEnvironment()
{
    auto const settingsDirectory = qEnvironmentVariable(SettingsDirectoryVariable);
    if (settingsDirectory.isEmpty())
        return;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory);
}

} // namespace DbtoolGui
