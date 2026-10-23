// SPDX-License-Identifier: Apache-2.0
//
// Keeps the dbtool-gui test executables away from the user's own dbtool state: their profile
// files (which may name customer or production servers), their settings and their backups.

#pragma once

#include "../SettingsLocation.hpp"

#include <Lightweight/SqlMigration.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <Config/ConfigDiscovery.hpp>
#include <Config/ProfileStore.hpp>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QtEnvironmentVariables>
#include <QtCore/QtLogging>

namespace DbtoolGui::Tests
{

/// For the lifetime of the object, runs the process in a fresh temporary directory that holds
/// an empty `dbtool.yml` declaring `root: true`, and starts config discovery there
/// (`LIGHTWEIGHT_DBTOOL_CONFIG_SEARCH_DIR`), so no file next to the executable, further up or in
/// the user's profile is loaded. The per-user `dbtool.yml`, the QSettings and the temporary
/// directory (`TMP`, `TEMP`) all live under that directory too, and backups go to `settings/backups` there, which
/// `LIGHTWEIGHT_DBTOOL_GUI_SETTINGS_DIR` also makes the default backup folder. Construct it in
/// `main` right after the error-dialog guard, before the application object or any test exists.
class ScopedTestIsolation
{
  public:
    ScopedTestIsolation():
        _root { QDir::tempPath() + QStringLiteral("/dbtool-gui-test-isolation-XXXXXX") },
        _previousWorkingDirectory { std::filesystem::current_path() }
    {
        auto const root = std::filesystem::path { _root.path().toStdWString() };
        auto const workingDirectory = root / "work";
        auto const userConfigDirectory = root / "user-config";
        auto const settingsDirectory = root / "settings";
        auto const backupDirectory = settingsDirectory / "backups";
        auto const temporaryDirectory = root / "tmp";
        std::filesystem::create_directories(workingDirectory);
        std::filesystem::create_directories(userConfigDirectory);
        std::filesystem::create_directories(backupDirectory);
        std::filesystem::create_directories(temporaryDirectory);
        std::ofstream(workingDirectory / "dbtool.yml", std::ios::binary)
            << "root: true\ndefaultBackupDir: \"" << Utf8(backupDirectory.generic_u8string()) << "\"\nprofiles: {}\n";

        SetVariable(std::string { Lightweight::Config::ConfigSearchDirectoryVariable }.c_str(), workingDirectory);
        SetVariable(Lightweight::Config::ProfileStore::UserConfigDirectoryVariable, userConfigDirectory);
        SetVariable(SettingsDirectoryVariable, settingsDirectory);
        SetVariable("TMP", temporaryDirectory);
        SetVariable("TEMP", temporaryDirectory);
        UseSettingsDirectoryFromEnvironment();
        QStandardPaths::setTestModeEnabled(true);
        std::filesystem::current_path(workingDirectory);

        qInfo("Test isolation: working directory %s, profile file %s, user profile file %s",
              Utf8(workingDirectory.u8string()).c_str(),
              Utf8((workingDirectory / "dbtool.yml").u8string()).c_str(),
              Utf8(Lightweight::Config::ProfileStore::DefaultPath().u8string()).c_str());
    }

    ScopedTestIsolation(ScopedTestIsolation const&) = delete;
    ScopedTestIsolation(ScopedTestIsolation&&) = delete;
    ScopedTestIsolation& operator=(ScopedTestIsolation const&) = delete;
    ScopedTestIsolation& operator=(ScopedTestIsolation&&) = delete;

    /// Closes the migration manager's connection and leaves the temporary directory, so that it
    /// can be removed with the databases the tests created in it.
    ~ScopedTestIsolation()
    {
        Lightweight::SqlMigration::MigrationManager::GetInstance().CloseDataMapper();
        auto ignored = std::error_code {};
        std::filesystem::current_path(_previousWorkingDirectory, ignored);
    }

  private:
    /// Returns the UTF-8 bytes of @p text; path::string() throws on Windows for names outside the ANSI code page.
    static std::string Utf8(std::u8string const& text)
    {
        return { text.begin(), text.end() };
    }

    static void SetVariable(char const* name, std::filesystem::path const& directory)
    {
        qputenv(name, QFile::encodeName(QString::fromStdWString(directory.wstring())));
    }

    QTemporaryDir _root;
    std::filesystem::path _previousWorkingDirectory;
};

} // namespace DbtoolGui::Tests
