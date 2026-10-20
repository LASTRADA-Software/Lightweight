// SPDX-License-Identifier: Apache-2.0

#include "../../AppController.hpp"
#include "../../LogLevel.hpp"
#include "../../MigrationRunner.hpp"
#include "../../StderrMessageHandler.hpp"
#include "../SqliteDriver.hpp"
#include "WorkflowFixture.hpp"

#include <Lightweight/SqlStatement.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <Config/ProfileStore.hpp>
#include <PluginIngestion.hpp>
#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QFutureWatcher>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>
#include <QtCore/QtEnvironmentVariables>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlEngine>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    // odbcinst.h needs the Windows types; keep it after windows.h.
    #include <odbcinst.h>
#endif

namespace DbtoolGui::Tests
{

namespace
{
    std::mutex RecordedQmlWarningsMutex;
    QStringList RecordedQmlWarnings;
    std::mutex OpenedUrlsMutex;
    QStringList OpenedUrls;
} // namespace

void RecordOpenedUrl(QUrl const& url)
{
    std::scoped_lock const lock { OpenedUrlsMutex };
    OpenedUrls.push_back(url.toString());
}

QStringList WorkflowFixture::openedUrls() const
{
    std::scoped_lock const lock { OpenedUrlsMutex };
    return OpenedUrls;
}

void RecordQmlWarningsAndWriteToStderr(QtMsgType const type, QMessageLogContext const& context, QString const& message)
{
    if (type == QtWarningMsg && message.contains(QStringLiteral(".qml")))
    {
        std::scoped_lock const lock { RecordedQmlWarningsMutex };
        RecordedQmlWarnings.push_back(message);
    }
    WriteQtMessageToStderr(type, context, message);
}

QStringList WorkflowFixture::qmlWarnings() const
{
    std::scoped_lock const lock { RecordedQmlWarningsMutex };
    return RecordedQmlWarnings;
}

namespace
{

    /// A fixture plugin: the key tests name it by and the module CMake built.
    struct FixtureModule
    {
        std::string_view key;
        char const* path;
    };

    constexpr auto FixtureModules = std::array {
        FixtureModule { .key = "workflow", .path = DBTOOL_GUI_WORKFLOW_FIXTURE_WORKFLOW },
        FixtureModule { .key = "recorder", .path = DBTOOL_GUI_WORKFLOW_FIXTURE_RECORDER },
        FixtureModule { .key = "backupRequiredRelease", .path = DBTOOL_GUI_WORKFLOW_FIXTURE_BACKUP_REQUIRED_RELEASE },
        FixtureModule { .key = "migrationRequiresBackup", .path = DBTOOL_GUI_WORKFLOW_FIXTURE_MIGRATION_REQUIRES_BACKUP },
        FixtureModule { .key = "unicodeCandidate", .path = DBTOOL_GUI_WORKFLOW_FIXTURE_UNICODE_CANDIDATE },
        FixtureModule { .key = "failing", .path = DBTOOL_GUI_WORKFLOW_FIXTURE_FAILING },
        FixtureModule { .key = "bulk", .path = DBTOOL_GUI_WORKFLOW_FIXTURE_BULK },
    };

    std::filesystem::path ToPath(QString const& path)
    {
        return std::filesystem::path { path.toStdWString() };
    }

    /// @p text as a double-quoted YAML scalar.
    QString YamlQuoted(QString text)
    {
        text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        text.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        return QLatin1Char('"') + text + QLatin1Char('"');
    }

    /// @p connectionString with its `DATABASE=` entry set to @p database, appended when it has none.
    QString WithDatabase(QString connectionString, QString const& database)
    {
        static auto const entry =
            QRegularExpression { QStringLiteral("(^|;)\\s*DATABASE\\s*=[^;]*"), QRegularExpression::CaseInsensitiveOption };
        if (connectionString.contains(entry))
            return connectionString.replace(entry, QStringLiteral("\\1DATABASE=") + database);
        if (!connectionString.endsWith(QLatin1Char(';')))
            connectionString += QLatin1Char(';');
        return connectionString + QStringLiteral("DATABASE=") + database;
    }

    /// The database file of a SQLite connection string, or @p database itself when it is a plain path.
    QString SqliteDatabaseFile(QString const& database)
    {
        static auto const entry = QRegularExpression { QStringLiteral("(?:^|;)\\s*DATABASE\\s*=([^;]*)"),
                                                       QRegularExpression::CaseInsensitiveOption };
        auto const match = entry.match(database);
        return match.hasMatch() ? match.captured(1).trimmed() : database;
    }

    Lightweight::SqlConnectionString ToConnectionString(QString const& connectionString)
    {
        return Lightweight::SqlConnectionString { .value = connectionString.toStdString() };
    }

    /// Runs @p work on a pool thread while a local event loop keeps the GUI thread, and with it the
    /// watchdog, responsive; returns its result and rethrows what it throws.
    template <typename Work>
    std::invoke_result_t<Work&> OffGuiThread(Work work)
    {
        using Result = std::invoke_result_t<Work&>;
        using Stored = std::conditional_t<std::is_void_v<Result>, bool, Result>;
        auto result = std::optional<Stored> {};
        auto error = std::exception_ptr {};
        auto loop = QEventLoop {};
        auto watcher = QFutureWatcher<void> {};
        QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([&] {
            try
            {
                if constexpr (std::is_void_v<Result>)
                {
                    work();
                    result.emplace(true);
                }
                else
                    result.emplace(work());
            }
            catch (...)
            {
                error = std::current_exception();
            }
        }));
        if (!watcher.isFinished())
            loop.exec();
        watcher.waitForFinished();
        if (error)
            std::rethrow_exception(error);
        if constexpr (!std::is_void_v<Result>)
            return std::move(*result);
    }

    /// Runs @p sql on a new connection to @p connectionString; throws on failure.
    void ExecuteOn(QString const& connectionString, std::string_view sql)
    {
        auto connection = Lightweight::SqlConnection { ToConnectionString(connectionString) };
        auto statement = Lightweight::SqlStatement { connection };
        static_cast<void>(statement.ExecuteDirect(sql));
    }

} // namespace

WorkflowFixture::WorkflowFixture(QQmlEngine* engine, QObject* parent):
    QObject(parent),
    _engine { engine },
    _root { QDir::tempPath() + QStringLiteral("/dbtool-gui-workflow-XXXXXX") }
{
}

WorkflowFixture::~WorkflowFixture()
{
    // No local event loops here: the engine is being torn down.
    _probe.Stop();
    ReleaseLock();
    removeUserDsns();
    DropDatabases();
    restoreEnv();
}

QString WorkflowFixture::newPluginsDir(QStringList const& fixtureKeys)
{
    auto const directory = NextPath(QStringLiteral("plugins"));
    auto const target = ToPath(directory);
    std::filesystem::create_directories(target);
    for (auto const& key: fixtureKeys)
    {
        if (key == QStringLiteral("broken"))
        {
            // Named like a plugin but no loadable library, so loading it fails.
            auto const extension = std::filesystem::path { FixtureModules.front().path }.extension().string();
            std::ofstream { target / ("brokenPlugin" + extension), std::ios::binary } << "not a shared library";
            continue;
        }
        auto const module = std::ranges::find_if(
            FixtureModules, [&key](FixtureModule const& entry) { return key.toStdString() == entry.key; });
        if (module == FixtureModules.end())
        {
            ThrowError(QStringLiteral("unknown fixture plugin key: %1").arg(key));
            return {};
        }
        auto const source = std::filesystem::path { module->path };
        auto ignored = std::error_code {};
        if (!std::filesystem::copy_file(source,
                                        target / (key.toStdString() + "Plugin" + source.extension().string()),
                                        std::filesystem::copy_options::overwrite_existing,
                                        ignored))
        {
            ThrowError(QStringLiteral("cannot copy the fixture plugin %1: %2")
                           .arg(QString::fromStdWString(source.wstring()), QString::fromStdString(ignored.message())));
            return {};
        }
    }
    return directory;
}

QString WorkflowFixture::newSqliteDatabase()
{
    auto const file = NextPath(QStringLiteral("database")) + QStringLiteral(".sqlite");
    return QString::fromStdString(SqliteConnectionString(file.toStdString()));
}

bool WorkflowFixture::mssqlAvailable() const
{
    return !qEnvironmentVariable(MssqlConnectionStringVariable).isEmpty();
}

QString WorkflowFixture::newMssqlDatabase()
{
    auto const master = qEnvironmentVariable(MssqlConnectionStringVariable);
    if (master.isEmpty())
    {
        ThrowError(QStringLiteral("%1 is not set").arg(QLatin1StringView { MssqlConnectionStringVariable }));
        return {};
    }
    auto const name = QStringLiteral("dbtool_gui_%1_%2").arg(QCoreApplication::applicationPid()).arg(++_sequence);
    try
    {
        OffGuiThread([&] { ExecuteOn(master, std::format("CREATE DATABASE [{}]", name.toStdString())); });
    }
    catch (std::exception const& error)
    {
        ThrowError(QStringLiteral("cannot create the database %1: %2").arg(name, QString::fromUtf8(error.what())));
        return {};
    }
    _mssqlDatabases.append(name);
    return WithDatabase(master, name);
}

QString WorkflowFixture::writeProfileFile(QVariantList const& profiles, QString const& defaultProfile)
{
    auto const path = tempPath(QStringLiteral("dbtool.yml"));
    auto file = QFile { path };
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        ThrowError(QStringLiteral("cannot write %1: %2").arg(path, file.errorString()));
        return {};
    }
    auto stream = QTextStream { &file };
    if (!defaultProfile.isEmpty())
        stream << "defaultProfile: " << YamlQuoted(defaultProfile) << '\n';
    stream << (profiles.isEmpty() ? "profiles: {}\n" : "profiles:\n");
    for (auto const& entry: profiles)
    {
        auto const profile = entry.toMap();
        stream << "  " << YamlQuoted(profile.value(QStringLiteral("name")).toString()) << ":\n";
        for (auto const& [key, value]: profile.asKeyValueRange())
        {
            if (key != QStringLiteral("name"))
                stream << "    " << key << ": " << YamlQuoted(value.toString()) << '\n';
        }
    }
    return path;
}

QVariant WorkflowFixture::queryProfileField(QString const& file, QString const& profile, QString const& field)
{
    auto const store = Lightweight::Config::ProfileStore::LoadOrDefault(ToPath(file));
    if (!store)
    {
        ThrowError(QStringLiteral("cannot read %1: %2").arg(file, QString::fromStdString(store.error())));
        return {};
    }
    auto const* const found = store->Find(profile.toStdString());
    if (found == nullptr)
        return {};
    auto const text = [](std::string const& value) {
        return QVariant { QString::fromStdString(value) };
    };
    auto const path = [](std::filesystem::path const& value) {
        return QVariant { QString::fromStdU16String(value.u16string()) };
    };
    if (field == QStringLiteral("connectionString"))
        return text(found->connectionString);
    if (field == QStringLiteral("pluginsDir"))
        return path(found->pluginsDir);
    if (field == QStringLiteral("schema"))
        return text(found->schema);
    if (field == QStringLiteral("dsn"))
        return text(found->dsn);
    if (field == QStringLiteral("uid"))
        return text(found->uid);
    if (field == QStringLiteral("secretRef"))
        return text(found->secretRef);
    if (field == QStringLiteral("password"))
        return text(found->password);
    if (field == QStringLiteral("backupDir"))
        return path(found->backupDir);
    return {};
}

void WorkflowFixture::rewriteFile(QString const& path)
{
    auto file = QFile { path };
    if (!file.open(QIODevice::ReadWrite))
    {
        ThrowError(QStringLiteral("cannot open %1: %2").arg(path, file.errorString()));
        return;
    }
    auto const content = file.readAll();
    file.seek(0);
    if (file.write(content) != content.size())
        ThrowError(QStringLiteral("cannot write %1: %2").arg(path, file.errorString()));
}

void WorkflowFixture::setEnv(QString const& name, QString const& value)
{
    RememberEnv(name);
    qputenv(name.toLocal8Bit().constData(), value.toLocal8Bit());
}

void WorkflowFixture::unsetEnv(QString const& name)
{
    RememberEnv(name);
    qunsetenv(name.toLocal8Bit().constData());
}

void WorkflowFixture::restoreEnv()
{
    for (auto const& [name, value]: _originalEnvironment)
    {
        auto const variable = name.toLocal8Bit();
        if (value.has_value())
            qputenv(variable.constData(), *value);
        else
            qunsetenv(variable.constData());
    }
    _originalEnvironment.clear();
}

void WorkflowFixture::setSetting(QString const& key, QVariant const& value)
{
    auto settings = QSettings {};
    settings.setValue(key, value);
    settings.sync();
}

QVariant WorkflowFixture::setting(QString const& key) const
{
    return QSettings {}.value(key);
}

QVariant WorkflowFixture::queryScalar(QString const& connectionString, QString const& sql)
{
    try
    {
        auto const value = OffGuiThread([&] {
            auto connection = Lightweight::SqlConnection { ToConnectionString(connectionString) };
            auto statement = Lightweight::SqlStatement { connection };
            auto cursor = statement.ExecuteDirect(sql.toStdString());
            return cursor.FetchRow() ? cursor.GetColumn<std::optional<std::string>>(1) : std::nullopt;
        });
        if (!value.has_value())
            return {};
        auto const text = QString::fromStdString(*value);
        auto isNumber = false;
        auto const number = text.toLongLong(&isNumber);
        return isNumber ? QVariant { number } : QVariant { text };
    }
    catch (std::exception const& error)
    {
        ThrowError(QStringLiteral("query failed: %1: %2").arg(sql, QString::fromUtf8(error.what())));
        return {};
    }
}

void WorkflowFixture::executeSql(QString const& connectionString, QString const& sql)
{
    try
    {
        OffGuiThread([&] {
            auto connection = Lightweight::SqlConnection { ToConnectionString(connectionString) };
            auto statement = Lightweight::SqlStatement { connection };
            statement.ExecuteDirect(sql.toStdString());
            return 0;
        });
    }
    catch (std::exception const& error)
    {
        ThrowError(QStringLiteral("statement failed: %1: %2").arg(sql, QString::fromUtf8(error.what())));
    }
}

int WorkflowFixture::lineCount(QString const& path) const
{
    auto stream = std::ifstream { ToPath(path) };
    auto lines = 0;
    for (auto line = std::string {}; std::getline(stream, line);)
        ++lines;
    return lines;
}

QUrl WorkflowFixture::localFileUrl(QString const& path) const
{
    return QUrl::fromLocalFile(path);
}

qint64 WorkflowFixture::fileSize(QString const& path) const
{
    QFileInfo const info { path };
    return info.exists() ? info.size() : -1;
}

bool WorkflowFixture::fileExists(QString const& path) const
{
    return QFileInfo::exists(path);
}

QString WorkflowFixture::fileText(QString const& path) const
{
    auto file = QFile { path };
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString WorkflowFixture::tempPath(QString const& name) const
{
    return _root.filePath(name);
}

int WorkflowFixture::runDbtool(QStringList const& arguments, QVariantMap const& environment, int timeoutMs)
{
    auto process = QProcess {};
    auto processEnvironment = QProcessEnvironment::systemEnvironment();
    for (auto const& [name, value]: environment.asKeyValueRange())
        processEnvironment.insert(name, value.toString());
    process.setProcessEnvironment(processEnvironment);
    process.setProcessChannelMode(QProcess::MergedChannels);

    auto loop = QEventLoop {};
    auto exitCode = -1;
    connect(&process, &QProcess::finished, &loop, [&](int code, QProcess::ExitStatus status) {
        exitCode = status == QProcess::NormalExit ? code : -1;
        loop.quit();
    });
    connect(&process, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            loop.quit();
    });
    QTimer::singleShot(std::chrono::milliseconds { timeoutMs }, &loop, &QEventLoop::quit);
    process.start(QString::fromUtf8(DBTOOL_GUI_WORKFLOW_DBTOOL), arguments);
    // A local loop instead of waitForFinished keeps the GUI thread, and with it the watchdog, alive.
    loop.exec();
    auto const timedOut = process.state() != QProcess::NotRunning;
    if (timedOut)
    {
        process.kill();
        process.waitForFinished();
    }
    if (timedOut || exitCode != 0)
    {
        // The command line is left out on purpose: a connection string in it can carry a password.
        auto const reason = timedOut ? QStringLiteral("ran over %1 ms").arg(timeoutMs)
                                     : (exitCode == -1 ? QStringLiteral("did not start or crashed")
                                                       : QStringLiteral("exited with code %1").arg(exitCode));
        auto const output = QString::fromLocal8Bit(process.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        ThrowError(QStringLiteral("dbtool %1; its output:\n%2").arg(reason, output));
        return timedOut ? -1 : exitCode;
    }
    return exitCode;
}

namespace
{

    /// The `DBTOOL_GUI_WORKFLOW_*` settings and the problems with them, as `externalRun` and
    /// `externalRunErrors` return them.
    struct ExternalRunReading
    {
        QVariantMap run;
        QStringList errors;
    };

    ExternalRunReading ReadExternalRun()
    {
        auto reading = ExternalRunReading {};
        auto& run = reading.run;
        auto& errors = reading.errors;
        run = QVariantMap {
            { QStringLiteral("pluginsDir"), qEnvironmentVariable("DBTOOL_GUI_WORKFLOW_PLUGINS_DIR") },
            { QStringLiteral("profileFile"), qEnvironmentVariable("DBTOOL_GUI_WORKFLOW_PROFILE_FILE") },
            { QStringLiteral("profile"), qEnvironmentVariable("DBTOOL_GUI_WORKFLOW_PROFILE") },
            { QStringLiteral("skipFixtures"), false },
            { QStringLiteral("longRunTimeoutMs"), WorkflowFixture::DefaultLongRunTimeoutMs },
            { QStringLiteral("expectationsFile"), qEnvironmentVariable("DBTOOL_GUI_WORKFLOW_EXPECTATIONS") },
        };

        auto const skipFixtures = qEnvironmentVariable("DBTOOL_GUI_WORKFLOW_SKIP_FIXTURES");
        if (skipFixtures == QStringLiteral("1"))
            run.insert(QStringLiteral("skipFixtures"), true);
        else if (!skipFixtures.isEmpty() && skipFixtures != QStringLiteral("0"))
            errors.append(QStringLiteral("DBTOOL_GUI_WORKFLOW_SKIP_FIXTURES must be 0 or 1, not \"%1\"").arg(skipFixtures));

        if (auto const timeout = qEnvironmentVariable("DBTOOL_GUI_WORKFLOW_LONG_RUN_TIMEOUT_MS"); !timeout.isEmpty())
        {
            auto isNumber = false;
            auto const milliseconds = timeout.toInt(&isNumber);
            if (isNumber && milliseconds > 0)
                run.insert(QStringLiteral("longRunTimeoutMs"), milliseconds);
            else
                errors.append(
                    QStringLiteral("DBTOOL_GUI_WORKFLOW_LONG_RUN_TIMEOUT_MS must be a positive integer, not \"%1\"")
                        .arg(timeout));
        }

        auto const expectationsPath = run.value(QStringLiteral("expectationsFile")).toString();
        if (expectationsPath.isEmpty())
            return reading;
        auto file = QFile { expectationsPath };
        if (!file.open(QIODevice::ReadOnly))
        {
            errors.append(QStringLiteral("DBTOOL_GUI_WORKFLOW_EXPECTATIONS: cannot read %1: %2")
                              .arg(expectationsPath, file.errorString()));
            return reading;
        }
        auto parseError = QJsonParseError {};
        auto const document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (!document.isObject())
        {
            errors.append(QStringLiteral("DBTOOL_GUI_WORKFLOW_EXPECTATIONS: %1 holds no JSON object: %2")
                              .arg(expectationsPath,
                                   parseError.error == QJsonParseError::NoError ? QStringLiteral("not an object")
                                                                                : parseError.errorString()));
            return reading;
        }
        run.insert(QStringLiteral("expectations"), document.object().toVariantMap());
        return reading;
    }

} // namespace

QVariantMap WorkflowFixture::externalRun() const
{
    auto reading = ReadExternalRun();
    if (!reading.errors.isEmpty())
        ThrowError(reading.errors.join(QLatin1Char('\n')));
    return reading.run;
}

QStringList WorkflowFixture::externalRunErrors() const
{
    return ReadExternalRun().errors;
}

int WorkflowFixture::longRunTimeoutMs() const
{
    return ReadExternalRun().run.value(QStringLiteral("longRunTimeoutMs")).toInt();
}

QString WorkflowFixture::copyProfileFileWithoutPluginsDirs(QString const& file)
{
    auto const source = ToPath(file);
    auto store = Lightweight::Config::ProfileStore::LoadOrDefault(source);
    if (!store)
    {
        ThrowError(QStringLiteral("cannot read %1: %2").arg(file, QString::fromStdString(store.error())));
        return {};
    }
    auto const base = std::filesystem::absolute(source).parent_path();
    auto const anchored = [&base](std::filesystem::path const& path) {
        return path.empty() || path.is_absolute() ? path : base / path;
    };
    store->SetDefaultPluginsDir({});
    store->SetDefaultBackupDir(anchored(store->DefaultBackupDir()));
    for (auto profile: store->Profiles())
    {
        profile.pluginsDir.clear();
        profile.backupDir = anchored(profile.backupDir);
        store->Upsert(std::move(profile));
    }
    auto const copy = NextPath(QStringLiteral("profiles")) + QStringLiteral("/dbtool.yml");
    if (auto const saved = store->Save(ToPath(copy)); !saved)
    {
        ThrowError(QStringLiteral("cannot write %1: %2").arg(copy, QString::fromStdString(saved.error())));
        return {};
    }
    return copy;
}

QString WorkflowFixture::clipboardText() const
{
    return QGuiApplication::clipboard()->text();
}

void WorkflowFixture::holdMigrationLock(QString const& connectionString)
{
    releaseMigrationLock();
    try
    {
        auto failure = OffGuiThread([&]() -> std::string {
            _lockConnection = std::make_unique<Lightweight::SqlConnection>(ToConnectionString(connectionString));
            auto lock = Lightweight::SqlScopedLock::TryConstruct(
                *_lockConnection, Lightweight::Tools::MigrationLockName, std::chrono::seconds { 5 });
            if (!lock.has_value())
            {
                _lockConnection.reset();
                return lock.error().message;
            }
            _heldLock.emplace(std::move(*lock));
            return {};
        });
        if (!failure.empty())
            ThrowError(QStringLiteral("cannot take the migration lock: %1").arg(QString::fromStdString(failure)));
    }
    catch (std::exception const& error)
    {
        _lockConnection.reset();
        ThrowError(QStringLiteral("cannot take the migration lock: %1").arg(QString::fromUtf8(error.what())));
    }
}

void WorkflowFixture::releaseMigrationLock()
{
    if (_lockConnection)
        OffGuiThread([this] { ReleaseLock(); });
}

void WorkflowFixture::ReleaseLock()
{
    _heldLock.reset();
    _lockConnection.reset();
}

void WorkflowFixture::setLockTimeoutMs(int milliseconds)
{
    if (auto* const controller = Controller(); controller != nullptr)
        controller->runner()->setLockTimeout(std::chrono::milliseconds { milliseconds });
}

void WorkflowFixture::detachMigrationManager()
{
    if (auto* const controller = Controller(); controller != nullptr)
        controller->runner()->SetManager(nullptr);
}

void WorkflowFixture::emitRunnerLogLines(int count)
{
    auto* const controller = Controller();
    if (controller == nullptr)
        return;
    for (auto const line: std::views::iota(1, count + 1))
        Q_EMIT controller->runner()->logLine(QStringLiteral("Runner log line %1 of %2").arg(line).arg(count),
                                             LogLevel::Info);
}

void WorkflowFixture::startDbAccessProbe()
{
    _probe.Start();
}

int WorkflowFixture::dbAccessCount() const
{
    return _probe.Count();
}

bool WorkflowFixture::addUserDsn(QString const& name, QString const& sqliteDatabase)
{
    auto const database = QDir::toNativeSeparators(SqliteDatabaseFile(sqliteDatabase));
#ifdef _WIN32
    // Never replace a DSN of the user's own.
    auto existingDriver = std::array<wchar_t, 256> {};
    SQLSetConfigMode(ODBC_USER_DSN);
    auto const existing = SQLGetPrivateProfileStringW(reinterpret_cast<LPCWSTR>(name.utf16()),
                                                      L"Driver",
                                                      L"",
                                                      existingDriver.data(),
                                                      static_cast<int>(existingDriver.size()),
                                                      L"ODBC.INI");
    SQLSetConfigMode(ODBC_BOTH_DSN);
    if (existing > 0 && !_userDsns.contains(name))
        return false;
    // Attributes: "key=value" entries, each NUL-terminated, the list ending in an empty one.
    auto attributes = QStringLiteral("DSN=%1").arg(name) + QChar { 0 } + QStringLiteral("Database=%1").arg(database)
                      + QChar { 0 } + QChar { 0 };
    auto const driver = QString::fromLatin1(SqliteDriver);
    if (!SQLConfigDataSourceW(
            nullptr, ODBC_ADD_DSN, reinterpret_cast<LPCWSTR>(driver.utf16()), reinterpret_cast<LPCWSTR>(attributes.utf16())))
        return false;
    _userDsns[name] = database;
    return true;
#else
    _userDsns[name] = database;
    return WriteUserDsns();
#endif
}

void WorkflowFixture::removeUserDsns()
{
#ifdef _WIN32
    auto const driver = QString::fromLatin1(SqliteDriver);
    for (auto const& [name, database]: _userDsns)
    {
        auto const attributes = QStringLiteral("DSN=%1").arg(name) + QChar { 0 } + QChar { 0 };
        static_cast<void>(SQLConfigDataSourceW(nullptr,
                                               ODBC_REMOVE_DSN,
                                               reinterpret_cast<LPCWSTR>(driver.utf16()),
                                               reinterpret_cast<LPCWSTR>(attributes.utf16())));
    }
    _userDsns.clear();
#else
    _userDsns.clear();
    static_cast<void>(WriteUserDsns());
#endif
}

void WorkflowFixture::cleanupDatabases()
{
    if (!_mssqlDatabases.isEmpty())
        OffGuiThread([this] { DropDatabases(); });
}

void WorkflowFixture::DropDatabases()
{
    auto const master = qEnvironmentVariable(MssqlConnectionStringVariable);
    for (auto const& name: std::exchange(_mssqlDatabases, {}))
    {
        try
        {
            ExecuteOn(master,
                      std::format("ALTER DATABASE [{0}] SET SINGLE_USER WITH ROLLBACK IMMEDIATE; DROP DATABASE [{0}]",
                                  name.toStdString()));
        }
        catch (std::exception const& error)
        {
            qWarning("Could not drop the test database %s: %s", qPrintable(name), error.what());
        }
    }
}

AppController* WorkflowFixture::Controller() const
{
    auto* const controller = _engine->singletonInstance<AppController*>(QStringLiteral("Lightweight.Migrations"),
                                                                        QStringLiteral("AppController"));
    if (controller == nullptr)
        ThrowError(QStringLiteral("AppController is not available"));
    return controller;
}

void WorkflowFixture::ThrowError(QString const& message) const
{
    _engine->throwError(message);
}

QString WorkflowFixture::NextPath(QString const& prefix)
{
    return tempPath(QStringLiteral("%1-%2").arg(prefix).arg(++_sequence));
}

void WorkflowFixture::RememberEnv(QString const& name)
{
    if (_originalEnvironment.contains(name))
        return;
    auto const variable = name.toLocal8Bit();
    _originalEnvironment[name] =
        qEnvironmentVariableIsSet(variable.constData()) ? std::optional { qgetenv(variable.constData()) } : std::nullopt;
}

bool WorkflowFixture::WriteUserDsns() const
{
    auto file = QFile { qEnvironmentVariable("ODBCINI") };
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    auto stream = QTextStream { &file };
    for (auto const& [name, database]: _userDsns)
        stream << '[' << name << "]\nDriver=" << SqliteDriver << "\nDatabase=" << database << '\n';
    return true;
}

} // namespace DbtoolGui::Tests
