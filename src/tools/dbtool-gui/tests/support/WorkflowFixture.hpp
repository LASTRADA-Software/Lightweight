// SPDX-License-Identifier: Apache-2.0
//
// The helper the dbtool-gui workflow tests reach from QML as the singleton
// `DbtoolGuiWorkflow.WorkflowFixture`: scratch plugin directories and databases, profile files,
// environment variables and settings, database reads, a second migrator holding the lock, and the
// database-access probe.

#pragma once

#include "DbAccessProbe.hpp"

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlScopedLock.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>

#include <QtCore/QObject>
#include <QtCore/QtLogging>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTemporaryDir>
#include <QtCore/QVariant>

class QQmlEngine;

namespace DbtoolGui
{
class AppController;
}

namespace DbtoolGui::Tests
{

/// Qt message handler of the workflow tests: writes every message like `WriteQtMessageToStderr` and
/// remembers the warnings that name a `.qml` file, which `WorkflowFixture::qmlWarnings` returns.
void RecordQmlWarningsAndWriteToStderr(QtMsgType type, QMessageLogContext const& context, QString const& message);

/// Test-side helper exposed to QML. Everything it creates lives under one temporary directory and
/// is removed, together with the SQL Server databases, user DSNs, environment changes and the held
/// lock, when the engine destroys it. Failures throw a JavaScript error into the calling test.
///
/// The helpers that reach a database or run `dbtool` do that work off the GUI thread and spin a
/// local event loop meanwhile, so they may run while the Main window is open: queued events (a
/// runner's results, for one) are delivered during the call.
class WorkflowFixture final: public QObject
{
    Q_OBJECT

  public:
    /// Environment variable with a SQL Server connection string to the `master` database; the
    /// SQL Server scenarios run only when it is set.
    static constexpr char const* MssqlConnectionStringVariable = "DBTOOL_GUI_TEST_MSSQL_CONNECTION_STRING";

    /// @param engine The engine the fixture throws errors into and reaches `AppController` through.
    /// @param parent The QObject parent.
    explicit WorkflowFixture(QQmlEngine* engine, QObject* parent = nullptr);
    WorkflowFixture(WorkflowFixture const&) = delete;
    WorkflowFixture(WorkflowFixture&&) = delete;
    WorkflowFixture& operator=(WorkflowFixture const&) = delete;
    WorkflowFixture& operator=(WorkflowFixture&&) = delete;
    /// Releases the lock, removes the DSNs and databases, and restores the environment.
    ~WorkflowFixture() override;

    /// Creates a new plugins directory holding the fixture plugins @p fixtureKeys, each copied as
    /// `<key>Plugin<ext>` so discovery picks it up. Keys: `workflow`, `recorder`,
    /// `backupRequiredRelease`, `migrationRequiresBackup`, `unicodeCandidate`, `failing`, `bulk`, and
    /// `broken`: a `brokenPlugin<ext>` file that is no library, so loading it fails.
    /// @return The directory.
    Q_INVOKABLE QString newPluginsDir(QStringList const& fixtureKeys);

    /// @return The ODBC connection string of a new, empty SQLite database file.
    Q_INVOKABLE QString newSqliteDatabase();

    /// @return Whether the SQL Server scenarios can run (`DBTOOL_GUI_TEST_MSSQL_CONNECTION_STRING` is set).
    Q_INVOKABLE bool mssqlAvailable() const;

    /// Creates the SQL Server database `dbtool_gui_<pid>_<n>`, dropped again by `cleanupDatabases`
    /// or when the fixture is destroyed.
    /// @return The connection string of `DBTOOL_GUI_TEST_MSSQL_CONNECTION_STRING` pointed at it.
    Q_INVOKABLE QString newMssqlDatabase();

    /// Writes a profile file, replacing the previous one at the same path.
    /// @param profiles One map per profile: `name` plus the profile's keys (`connectionString`,
    ///        `pluginsDir`, `schema`, `dsn`, `uid`, `password`), each written as a string.
    /// @param defaultProfile The `defaultProfile` entry; empty writes none.
    /// @return The file's path.
    Q_INVOKABLE QString writeProfileFile(QVariantList const& profiles, QString const& defaultProfile);

    /// Reads one field of a profile from a profile file, as the application parses it.
    /// @param file The profile file.
    /// @param profile The profile's name.
    /// @param field `connectionString`, `pluginsDir`, `schema`, `dsn`, `uid`, `secretRef`, `password` or `backupDir`.
    /// @return The field's text; undefined when the profile or the field does not exist.
    Q_INVOKABLE QVariant queryProfileField(QString const& file, QString const& profile, QString const& field);

    /// Writes the file @p path again with the content it has, so a file watcher sees a change.
    Q_INVOKABLE void rewriteFile(QString const& path);

    /// Sets the environment variable @p name to @p value until `restoreEnv`.
    Q_INVOKABLE void setEnv(QString const& name, QString const& value);

    /// Removes the environment variable @p name until `restoreEnv`.
    Q_INVOKABLE void unsetEnv(QString const& name);

    /// Gives every variable `setEnv` or `unsetEnv` touched its value from before the first change.
    Q_INVOKABLE void restoreEnv();

    /// Writes @p value to the application's settings under @p key.
    Q_INVOKABLE void setSetting(QString const& key, QVariant const& value);

    /// @return The application's setting @p key; invalid when unset.
    Q_INVOKABLE QVariant setting(QString const& key) const;

    /// Runs @p sql on a connection of its own and returns the first column of the first row: an
    /// integer when the value reads as one, else its text; undefined when there is no row or it is NULL.
    Q_INVOKABLE QVariant queryScalar(QString const& connectionString, QString const& sql);

    /// @return The number of lines in the file @p path; 0 when it does not exist.
    Q_INVOKABLE int lineCount(QString const& path) const;

    /// @return The path @p name inside the fixture's temporary directory; nothing is created.
    Q_INVOKABLE QString tempPath(QString const& name) const;

    /// Runs the `dbtool` CLI with @p arguments, the process environment plus @p environment, keeping
    /// the event loop running meanwhile.
    /// @return Its exit code; -1 when it could not start, crashed or ran over two minutes.
    Q_INVOKABLE int runDbtool(QStringList const& arguments, QVariantMap const& environment);

    /// @return The warnings Qt logged that name a `.qml` file, from the process start (a QML type
    ///         that fails to load cleanly logs one while the module loads).
    Q_INVOKABLE QStringList qmlWarnings() const;

    /// @return The clipboard's text.
    Q_INVOKABLE QString clipboardText() const;

    /// Takes the migration lock on a connection of its own to @p connectionString, as another
    /// migrator would, and holds it until `releaseMigrationLock`.
    Q_INVOKABLE void holdMigrationLock(QString const& connectionString);

    /// Releases the lock `holdMigrationLock` took; nothing when none is held.
    Q_INVOKABLE void releaseMigrationLock();

    /// Sets how long the controller's migration runs wait for the migration lock before they refuse.
    /// @param milliseconds The wait.
    Q_INVOKABLE void setLockTimeoutMs(int milliseconds);

    /// Takes the migration manager away from the controller's runner, so no migration run can start.
    Q_INVOKABLE void detachMigrationManager();

    /// Emits @p count info lines `Runner log line <i> of <count>` through the controller's migration runner.
    Q_INVOKABLE void emitRunnerLogLines(int count);

    /// Starts counting database accesses process-wide; see `DbAccessProbe`.
    Q_INVOKABLE void startDbAccessProbe();

    /// @return The database accesses counted since `startDbAccessProbe`.
    Q_INVOKABLE int dbAccessCount() const;

    /// Registers the user DSN @p name for the SQLite ODBC driver until `removeUserDsns`. On Windows
    /// it refuses a name the user already has a DSN of; elsewhere DSNs go to the file `$ODBCINI`
    /// names, which the test's `main` points into the isolation directory.
    /// @param name The DSN.
    /// @param sqliteDatabase The database file, or a connection string naming it with `Database=`.
    /// @return Whether the DSN was registered.
    Q_INVOKABLE bool addUserDsn(QString const& name, QString const& sqliteDatabase);

    /// Removes the user DSNs `addUserDsn` registered.
    Q_INVOKABLE void removeUserDsns();

    /// Drops the SQL Server databases `newMssqlDatabase` created.
    Q_INVOKABLE void cleanupDatabases();

  private:
    [[nodiscard]] AppController* Controller() const;
    void ReleaseLock();
    void DropDatabases();
    void ThrowError(QString const& message) const;
    [[nodiscard]] QString NextPath(QString const& prefix);
    void RememberEnv(QString const& name);
    [[nodiscard]] bool WriteUserDsns() const;

    QQmlEngine* _engine;
    QTemporaryDir _root;
    int _sequence = 0;
    std::map<QString, std::optional<QByteArray>> _originalEnvironment;
    QStringList _mssqlDatabases;
    std::map<QString, QString> _userDsns;
    std::unique_ptr<Lightweight::SqlConnection> _lockConnection;
    std::optional<Lightweight::SqlScopedLock> _heldLock;
    DbAccessProbe _probe;
};

} // namespace DbtoolGui::Tests
