// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Lightweight/SqlBackup/SqlBackup.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlLogger.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <PluginLoader.hpp>
#include "Credentials.hpp"

#include <QAbstractListModel>
#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#if defined(_WIN32) || defined(_WIN64)
    #include <sql.h>
    #include <sqlext.h>
    #include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct MigrationRow
{
    quint64 timestamp {};
    QString title;
    bool applied {};
};

class MigrationsModel: public QAbstractListModel
{
    Q_OBJECT

  public:
    enum Roles
    {
        TimestampRole = Qt::UserRole + 1,
        TitleRole,
        AppliedRole,
    };

    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(QModelIndex const& = {}) const override
    {
        return static_cast<int>(_rows.size());
    }

    [[nodiscard]] QVariant data(QModelIndex const& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(_rows.size()))
            return {};
        auto const& row = _rows[index.row()];
        switch (role)
        {
            case TimestampRole:
                return QVariant::fromValue(row.timestamp);
            case TitleRole:
                return row.title;
            case AppliedRole:
                return row.applied;
        }
        return {};
    }

    [[nodiscard]] QHash<int, QByteArray> roleNames() const override
    {
        return {
            { TimestampRole, "timestamp" },
            { TitleRole, "title" },
            { AppliedRole, "applied" },
        };
    }

    void setRows(std::vector<MigrationRow> rows)
    {
        beginResetModel();
        _rows = std::move(rows);
        endResetModel();
    }

  private:
    std::vector<MigrationRow> _rows;
};

// SqlLogger implementation that writes to stderr and notifies a callback.
class GuiSqlLogger: public Lightweight::SqlLogger
{
  public:
    using Callback = std::function<void(std::string)>;

    explicit GuiSqlLogger(Callback cb):
        Lightweight::SqlLogger(SupportBindLogging::No),
        _cb { std::move(cb) }
    {
    }

    void OnWarning(std::string_view const& message) override { emit_("WARN", message); }
    void OnError(Lightweight::SqlError code, std::source_location loc) override
    {
        if (!_lastSql.empty())
            emit_("ERROR", std::format("Last SQL: {}", _lastSql));
        emit_("ERROR", std::format("{} ({}:{})", Lightweight::SqlErrorCategory().message(static_cast<int>(code)), loc.file_name(), loc.line()));
    }
    void OnError(Lightweight::SqlErrorInfo const& info, std::source_location /*loc*/) override
    {
        if (!_lastSql.empty())
            emit_("ERROR", std::format("Last SQL: {}", _lastSql));
        emit_("ERROR", std::format("[{}] {}", info.sqlState, info.message));
    }
    void OnConnectionOpened(Lightweight::SqlConnection const& conn) override { emit_("CONN", std::format("Opened: {}", conn.ConnectionString().Sanitized())); }
    void OnConnectionClosed(Lightweight::SqlConnection const& /*conn*/) override { emit_("CONN", "Closed"); }
    void OnConnectionIdle(Lightweight::SqlConnection const& /*conn*/) override {}
    void OnConnectionReuse(Lightweight::SqlConnection const& /*conn*/) override {}
    void OnExecuteDirect(std::string_view const& query) override { _lastSql = query; }
    void OnPrepare(std::string_view const& query) override { _lastSql = query; }
    void OnBind(std::string_view const& /*name*/, std::string /*value*/) override {}
    void OnExecute(std::string_view const& query) override { _lastSql = query; }
    void OnExecuteBatch() override {}
    void OnFetchRow() override {}
    void OnFetchEnd() override {}
    void OnScopedTimerStart(std::string const& /*tag*/) override {}
    void OnScopedTimerStop(std::string const& /*tag*/) override {}

  private:
    void emit_(std::string_view level, std::string_view message)
    {
        using namespace std::chrono;
        auto const now = zoned_time { current_zone(), system_clock::now() };
        auto line = std::format("[{:%H:%M:%S}] [{:5}] {}\n", now, level, message);
        std::fputs(line.c_str(), stdout);
        std::lock_guard lock { _mutex };
        _cb(std::move(line));
    }

    Callback _cb;
    std::mutex _mutex;
    std::string _lastSql;
};

class Backend: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString databasePath READ databasePath WRITE setDatabasePath NOTIFY databasePathChanged)
    Q_PROPERTY(QString pluginPath READ pluginPath WRITE setPluginPath NOTIFY pluginPathChanged)
    Q_PROPERTY(QString odbcDsn READ odbcDsn WRITE setOdbcDsn NOTIFY odbcDsnChanged)
    Q_PROPERTY(bool useDsn READ useDsn WRITE setUseDsn NOTIFY useDsnChanged)
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)
    Q_PROPERTY(bool pluginLoaded READ pluginLoaded NOTIFY pluginLoadedChanged)
    Q_PROPERTY(bool dbConnected READ dbConnected NOTIFY dbConnectedChanged)
    Q_PROPERTY(int appliedCount READ appliedCount NOTIFY modelRefreshed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY modelRefreshed)
    Q_PROPERTY(MigrationsModel* migrations READ migrations CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double operationProgress READ operationProgress NOTIFY operationProgressChanged)
    Q_PROPERTY(QString operationLabel READ operationLabel NOTIFY operationLabelChanged)

  public:
    explicit Backend(QObject* parent = nullptr):
        QObject { parent },
        _sqlLogger { [this](std::string line) {
            QMetaObject::invokeMethod(this, [this, line = QString::fromStdString(line)]() mutable {
                _log += line;
                emit logChanged();
            }, Qt::QueuedConnection);
        } },
        _migrations { new MigrationsModel(this) }
    {
        Lightweight::SqlLogger::SetLogger(_sqlLogger);
        _databasePath = QStringLiteral("database.db");
        _pluginPath = QStringLiteral("");
        if (auto creds = CredentialStore::load())
        {
            _username = creds->username;
            _password = creds->password;
        }
    }

    [[nodiscard]] QString databasePath() const
    {
        return _databasePath;
    }
    void setDatabasePath(QString const& value)
    {
        if (_databasePath != value)
        {
            _databasePath = value;
            emit databasePathChanged();
        }
    }

    [[nodiscard]] QString pluginPath() const
    {
        return _pluginPath;
    }
    void setPluginPath(QString const& value)
    {
        if (_pluginPath != value)
        {
            _pluginPath = value;
            emit pluginPathChanged();
        }
    }

    [[nodiscard]] QString odbcDsn() const
    {
        return _odbcDsn;
    }
    void setOdbcDsn(QString const& value)
    {
        if (_odbcDsn != value)
        {
            _odbcDsn = value;
            emit odbcDsnChanged();
        }
    }

    [[nodiscard]] bool useDsn() const
    {
        return _useDsn;
    }
    void setUseDsn(bool value)
    {
        if (_useDsn != value)
        {
            _useDsn = value;
            emit useDsnChanged();
        }
    }

    [[nodiscard]] QString username() const { return _username; }
    void setUsername(QString const& value)
    {
        if (_username != value) { _username = value; emit usernameChanged(); }
    }

    [[nodiscard]] QString password() const { return _password; }
    void setPassword(QString const& value)
    {
        if (_password != value) { _password = value; emit passwordChanged(); }
    }

    Q_INVOKABLE QStringList availableOdbcDsns()
    {
        QStringList result;
#if defined(_WIN32) || defined(_WIN64)
        SQLHENV henv = SQL_NULL_HENV;
        if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv) != SQL_SUCCESS)
            return result;
        SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);

        SQLWCHAR dsn[SQL_MAX_DSN_LENGTH + 1] {};
        SQLWCHAR desc[256] {};
        SQLSMALLINT dsnLen = 0, descLen = 0;
        SQLUSMALLINT direction = SQL_FETCH_FIRST;
        while (SQLDataSourcesW(henv, direction, dsn, SQL_MAX_DSN_LENGTH, &dsnLen, desc, 256, &descLen) == SQL_SUCCESS)
        {
            result.append(QString::fromWCharArray(reinterpret_cast<wchar_t const*>(dsn), dsnLen));
            direction = SQL_FETCH_NEXT;
        }
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
#endif
        return result;
    }

    [[nodiscard]] QString status() const
    {
        return _status;
    }
    [[nodiscard]] QString log() const
    {
        return _log;
    }
    [[nodiscard]] bool pluginLoaded() const
    {
        return _loader != nullptr;
    }
    [[nodiscard]] bool dbConnected() const
    {
        return _dbConnected;
    }
    [[nodiscard]] int appliedCount() const
    {
        return _appliedCount;
    }
    [[nodiscard]] int totalCount() const
    {
        return _totalCount;
    }
    [[nodiscard]] MigrationsModel* migrations() const
    {
        return _migrations;
    }

    [[nodiscard]] bool busy() const
    {
        return _busy;
    }
    [[nodiscard]] double operationProgress() const
    {
        return _operationProgress;
    }
    [[nodiscard]] QString operationLabel() const
    {
        return _operationLabel;
    }

    ~Backend() override
    {
        if (_pluginWorker.joinable())
            _pluginWorker.detach();
        if (_worker.joinable())
            _worker.detach();
    }

  public slots:
    void connectWithCredentials(QString const& user, QString const& pass)
    {
        _username = user;
        _password = pass;
        loadPlugin();
    }

    void clearLog()
    {
        _log.clear();
        emit logChanged();
    }

    void setPathFromUrl(QUrl const& url)
    {
        if (url.isLocalFile())
            setPluginPath(url.toLocalFile());
    }

    void setDatabaseFromUrl(QUrl const& url)
    {
        if (url.isLocalFile())
            setDatabasePath(url.toLocalFile());
    }

    // Called by the Connect button — only establishes the DB connection, never loads plugin.
    void loadPlugin()
    {
        if (_username.isEmpty() && _password.isEmpty())
        {
            appLog("INFO", "No credentials on file — prompting user.");
            emit credentialsNeeded(QStringLiteral(""));
            return;
        }
        if (_connectWorkerRunning)
        {
            appLog("WARN", "Connection attempt already in progress — ignoring.");
            return;
        }
        _connectWorkerRunning = true;
        setStatus(QStringLiteral("Connecting…"));
        appLog("INFO", "Starting connection on worker thread.");

        if (_pluginWorker.joinable())
            _pluginWorker.detach();
        _pluginWorker = std::thread([this,
                                     useDsn   = _useDsn,
                                     dsn      = _odbcDsn.toStdString(),
                                     dbPath   = _databasePath.toStdString(),
                                     user     = _username.toStdString(),
                                     pass     = _password.toStdString()]() {
            try
            {
                std::string connStr;
                if (useDsn && !dsn.empty())
                {
                    connStr = std::format("DSN={};UID={};PWD={}", dsn, user, pass);
                    appLog("INFO", std::format("Connecting via DSN: {} (user: {})", dsn, user));
                }
                else
                {
                    connStr = std::format("DRIVER={};Database={};UID={};PWD={}", sqliteDriver(), dbPath, user, pass);
                    appLog("INFO", std::format("Connecting via driver to: {} (user: {})", dbPath, user));
                }

                appLog("INFO", "Setting default connection string.");
                Lightweight::SqlConnection::SetDefaultConnectionString(
                    Lightweight::SqlConnectionString { .value = connStr });

                appLog("INFO", "Opening connection...");
                Lightweight::SqlConnection conn;
                appLog("INFO", "Connection object created, checking IsAlive()...");
                if (!conn.IsAlive())
                    throw std::runtime_error("Connection opened but IsAlive() returned false");
                appLog("INFO", "Connection is alive.");

                QMetaObject::invokeMethod(this, [this]() {
                    CredentialStore::save({ .username = _username, .password = _password });
                    appLog("INFO", "Credentials saved.");
                    _connectWorkerRunning = false;
                    _dbConnected = true;
                    emit dbConnectedChanged();
                    setStatus(QStringLiteral("Connected."));
                }, Qt::QueuedConnection);
            }
            catch (std::exception const& e)
            {
                auto msg = QString::fromUtf8(e.what());
                appLog("ERROR", std::format("Connection failed: {}", e.what()));
                QMetaObject::invokeMethod(this, [this, msg]() {
                    _connectWorkerRunning = false;
                    _dbConnected = false;
                    emit dbConnectedChanged();
                    emit credentialsNeeded(msg);
                    setStatus(QStringLiteral("Connection error: ") + msg);
                }, Qt::QueuedConnection);
            }
        });
    }

    // Called only when the user explicitly picks a plugin file.
    void loadMigrationPlugin()
    {
        if (!_dbConnected)
        {
            appLog("WARN", "Cannot load plugin — not connected to database.");
            setStatus(QStringLiteral("Connect to a database first."));
            return;
        }
        auto const pluginFs = std::filesystem::path(_pluginPath.toStdString());
        if (_pluginPath.isEmpty() || !std::filesystem::is_regular_file(pluginFs))
        {
            appLog("WARN", std::format("Plugin file not found or not set: '{}'", pluginFs.string()));
            setStatus(QStringLiteral("Plugin file not found."));
            return;
        }
        if (_pluginWorkerRunning)
        {
            appLog("WARN", "Plugin load already in progress — ignoring.");
            return;
        }
        _pluginWorkerRunning = true;
        setStatus(QStringLiteral("Loading plugin…"));
        appLog("INFO", std::format("Starting plugin load on worker thread: {}", pluginFs.string()));

        if (_pluginWorker.joinable())
            _pluginWorker.detach();
        _pluginWorker = std::thread([this, pluginPath = _pluginPath.toStdString()]() {
            try
            {
                appLog("INFO", "PluginLoader: loading shared library...");
                auto loader = std::make_unique<Lightweight::Tools::PluginLoader>(
                    std::filesystem::path(pluginPath));
                appLog("INFO", "PluginLoader: looking up AcquireMigrationManager...");
                auto const acquire =
                    loader->GetFunction<Lightweight::SqlMigration::MigrationManager*()>("AcquireMigrationManager");
                if (!acquire)
                    throw std::runtime_error("Plugin does not export AcquireMigrationManager");

                appLog("INFO", "PluginLoader: acquiring migration manager...");
                auto* pluginManager = acquire();
                auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
                if (pluginManager && pluginManager != &manager)
                {
                    for (auto const& r: pluginManager->GetAllReleases())
                        manager.RegisterRelease(r.version, r.highestTimestamp);
                    for (auto const* m: pluginManager->GetAllMigrations())
                        manager.AddMigration(m);
                }
                appLog("INFO", "PluginLoader: calling CreateMigrationHistory()...");
                manager.CreateMigrationHistory();
                appLog("INFO", std::format("Plugin loaded: {} migration(s) registered.",
                                           manager.GetAllMigrations().size()));

                QMetaObject::invokeMethod(this, [this, loader = std::move(loader)]() mutable {
                    _loader = std::move(loader);
                    _pluginWorkerRunning = false;
                    setStatus(QStringLiteral("Plugin loaded."));
                    emit pluginLoadedChanged();
                    refresh();
                }, Qt::QueuedConnection);
            }
            catch (std::exception const& e)
            {
                auto msg = QString::fromUtf8(e.what());
                appLog("ERROR", std::format("Plugin load failed: {}", e.what()));
                QMetaObject::invokeMethod(this, [this, msg]() {
                    _pluginWorkerRunning = false;
                    _loader.reset();
                    emit pluginLoadedChanged();
                    setStatus(QStringLiteral("Plugin error: ") + msg);
                }, Qt::QueuedConnection);
            }
        });
    }

    void applyAll()
    {
        applyUpTo(std::numeric_limits<quint64>::max());
    }

    void applyUpTo(quint64 targetTimestamp)
    {
        if (!_loader)
        {
            setStatus(QStringLiteral("Load a plugin first."));
            return;
        }
        setStatus(QStringLiteral("Applying migrations…"));
        runAsync([this, targetTimestamp]() {
            try
            {
                auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
                auto const pending = manager.GetPending();
                int applied = 0;
                for (auto const* m: pending)
                {
                    if (m->GetTimestamp().value > targetTimestamp)
                        break;
                    appLog("INFO", std::format("Applying migration: {}", m->GetTitle()));
                    manager.ApplySingleMigration(*m);
                    ++applied;
                }
                auto msg = QString::fromStdString(std::format("Applied {} migration(s).", applied));
                appLog("INFO", msg.toStdString());
                QMetaObject::invokeMethod(this, [this, msg]() {
                    setStatus(msg);
                    refresh();
                }, Qt::QueuedConnection);
            }
            catch (std::exception const& e)
            {
                auto msg = QString::fromUtf8(e.what());
                appLog("ERROR", std::format("Apply failed: {}", e.what()));
                QMetaObject::invokeMethod(this, [this, msg]() {
                    setStatus(QStringLiteral("Error: ") + msg);
                    refresh();
                }, Qt::QueuedConnection);
            }
        });
    }

    void loadSchema()
    {
        setStatus(QStringLiteral("Loading schema…"));
        runAsync([this]() {
            try
            {
                appLog("INFO", "Reading schema from database...");
                Lightweight::SqlConnection conn;
                Lightweight::SqlStatement stmt { conn };
                auto const tables = Lightweight::SqlSchema::ReadAllTables(stmt, conn.DatabaseName(), {});

                QVariantList out;
                for (auto const& table: tables)
                {
                    QVariantMap t;
                    t.insert(QStringLiteral("name"), QString::fromStdString(table.name));
                    t.insert(QStringLiteral("schema"), QString::fromStdString(table.schema));
                    QVariantList cols;
                    for (auto const& col: table.columns)
                    {
                        QVariantMap c;
                        c.insert(QStringLiteral("name"), QString::fromStdString(col.name));
                        c.insert(QStringLiteral("type"), QString::fromStdString(col.dialectDependantTypeString));
                        c.insert(QStringLiteral("nullable"), col.isNullable);
                        c.insert(QStringLiteral("unique"), col.isUnique);
                        c.insert(QStringLiteral("primaryKey"), col.isPrimaryKey);
                        c.insert(QStringLiteral("foreignKey"), col.isForeignKey);
                        c.insert(QStringLiteral("autoIncrement"), col.isAutoIncrement);
                        if (col.foreignKeyConstraint.has_value())
                        {
                            auto const& fk = *col.foreignKeyConstraint;
                            auto const targetTable = QString::fromStdString(fk.primaryKey.table.table);
                            auto const targetCol = fk.primaryKey.columns.empty()
                                                       ? QString()
                                                       : QString::fromStdString(fk.primaryKey.columns.front());
                            c.insert(QStringLiteral("fkTarget"), QStringLiteral("%1.%2").arg(targetTable, targetCol));
                        }
                        cols.append(c);
                    }
                    t.insert(QStringLiteral("columns"), cols);
                    t.insert(QStringLiteral("indexCount"), static_cast<int>(table.indexes.size()));
                    out.append(t);
                }
                auto msg = QString::fromStdString(std::format("Loaded schema: {} table(s).", out.size()));
                appLog("INFO", msg.toStdString());
                QMetaObject::invokeMethod(this, [this, out, msg]() {
                    setStatus(msg);
                    emit schemaReady(out);
                }, Qt::QueuedConnection);
            }
            catch (std::exception const& e)
            {
                auto msg = QString::fromUtf8(e.what());
                appLog("ERROR", std::format("Schema load failed: {}", e.what()));
                QMetaObject::invokeMethod(this, [this, msg]() {
                    setStatus(QStringLiteral("Schema error: ") + msg);
                    emit schemaReady({});
                }, Qt::QueuedConnection);
            }
        });
    }

    void executeQuery(QString const& sqlText)
    {
        auto const trimmed = sqlText.trimmed();
        if (trimmed.isEmpty())
        {
            emit queryResultReady(QStringLiteral("(empty query)"));
            return;
        }
        setStatus(QStringLiteral("Executing query…"));
        runAsync([this, trimmed]() {
            try
            {
                appLog("INFO", std::format("Executing query: {}", trimmed.toStdString()));
                Lightweight::SqlConnection conn;
                Lightweight::SqlStatement stmt { conn };
                auto cursor = stmt.ExecuteDirect(trimmed.toStdString());
                SQLHSTMT const hstmt = stmt.NativeHandle();
                auto const ncols = cursor.NumColumnsAffected();

                if (ncols == 0)
                {
                    auto result = QString::fromStdString(
                        std::format("OK — {} row(s) affected.", cursor.NumRowsAffected()));
                    QMetaObject::invokeMethod(this, [this, result]() {
                        setStatus(QStringLiteral("Query executed."));
                        emit queryResultReady(result);
                    }, Qt::QueuedConnection);
                    return;
                }

                std::vector<QString> headers;
                headers.reserve(ncols);
                for (SQLUSMALLINT i = 1; i <= static_cast<SQLUSMALLINT>(ncols); ++i)
                {
                    SQLCHAR name[256] {};
                    SQLSMALLINT nameLen = 0;
                    SQLDescribeColA(hstmt, i, name, sizeof(name), &nameLen, nullptr, nullptr, nullptr, nullptr);
                    headers.emplace_back(QString::fromUtf8(reinterpret_cast<char const*>(name), nameLen));
                }

                std::vector<std::vector<QString>> rows;
                while (cursor.FetchRow())
                {
                    std::vector<QString> row;
                    row.reserve(ncols);
                    for (SQLUSMALLINT i = 1; i <= static_cast<SQLUSMALLINT>(ncols); ++i)
                    {
                        auto const val = cursor.GetNullableColumn<std::string>(i);
                        row.emplace_back(val.has_value() ? QString::fromStdString(*val) : QStringLiteral("NULL"));
                    }
                    rows.emplace_back(std::move(row));
                }

                std::vector<int> widths(ncols);
                for (size_t i = 0; i < ncols; ++i)
                    widths[i] = headers[i].size();
                for (auto const& row: rows)
                    for (size_t i = 0; i < ncols; ++i)
                        widths[i] = std::max<int>(widths[i], static_cast<int>(row[i].size()));

                auto appendRow = [&](std::vector<QString> const& cells, QString& out) {
                    for (size_t i = 0; i < ncols; ++i)
                    {
                        if (i > 0)
                            out += QStringLiteral("  │  ");
                        out += cells[i].leftJustified(widths[i], QLatin1Char(' '));
                    }
                    out += QLatin1Char('\n');
                };

                QString out;
                appendRow(headers, out);
                for (size_t i = 0; i < ncols; ++i)
                {
                    if (i > 0)
                        out += QStringLiteral("──┼──");
                    out += QString(widths[i], QChar(0x2500));
                }
                out += QLatin1Char('\n');
                for (auto const& row: rows)
                    appendRow(row, out);
                out += QString::fromStdString(std::format("\n{} row(s).", rows.size()));
                appLog("INFO", std::format("Query returned {} row(s).", rows.size()));
                QMetaObject::invokeMethod(this, [this, out]() {
                    setStatus(QStringLiteral("Query complete."));
                    emit queryResultReady(out);
                }, Qt::QueuedConnection);
            }
            catch (std::exception const& e)
            {
                auto msg = QStringLiteral("Error: ") + QString::fromUtf8(e.what());
                appLog("ERROR", std::format("Query failed: {}", e.what()));
                QMetaObject::invokeMethod(this, [this, msg]() {
                    setStatus(QStringLiteral("Query failed."));
                    emit queryResultReady(msg);
                }, Qt::QueuedConnection);
            }
        });
    }

    void migrationSql(quint64 timestamp)
    {
        if (!_loader)
        {
            emit migrationSqlReady(QStringLiteral("— Load a plugin first —"));
            return;
        }
        runAsync([this, timestamp]() {
            try
            {
                auto const& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
                auto const* migration =
                    manager.GetMigration(Lightweight::SqlMigration::MigrationTimestamp { timestamp });
                if (!migration)
                {
                    QMetaObject::invokeMethod(this, [this]() {
                        emit migrationSqlReady(QStringLiteral("Migration not found."));
                    }, Qt::QueuedConnection);
                    return;
                }
                Lightweight::SqlConnection conn;
                auto const& formatter = conn.QueryFormatter();
                Lightweight::SqlMigrationQueryBuilder builder { formatter };
                migration->Up(builder);
                auto const plan = std::move(builder).GetPlan();
                auto const statements = plan.ToSql();
                QString result;
                for (auto const& stmt: statements)
                    result += QString::fromStdString(stmt) + QStringLiteral(";\n\n");
                if (result.isEmpty())
                    result = QStringLiteral("(no SQL statements generated)");
                else
                    result = result.trimmed();
                QMetaObject::invokeMethod(this, [this, result]() {
                    emit migrationSqlReady(result);
                }, Qt::QueuedConnection);
            }
            catch (std::exception const& e)
            {
                auto msg = QStringLiteral("Error: ") + QString::fromUtf8(e.what());
                QMetaObject::invokeMethod(this, [this, msg]() {
                    emit migrationSqlReady(msg);
                }, Qt::QueuedConnection);
            }
        });
    }

    void refresh()
    {
        if (!_loader)
            return;
        runAsync([this]() {
            try
            {
                appLog("INFO", "Refreshing migration list...");
                auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
                auto const appliedIds = manager.GetAppliedMigrationIds();
                auto const& all = manager.GetAllMigrations();
                std::vector<MigrationRow> rows;
                rows.reserve(all.size());
                for (auto const* m: all)
                {
                    auto const ts = m->GetTimestamp().value;
                    bool const isApplied =
                        std::ranges::any_of(appliedIds, [&](auto id) { return id.value == ts; });
                    rows.push_back(MigrationRow {
                        .timestamp = ts, .title = QString::fromUtf8(m->GetTitle()), .applied = isApplied });
                }
                auto total = static_cast<int>(rows.size());
                auto applied = static_cast<int>(appliedIds.size());
                appLog("INFO", std::format("Refresh complete: {} total, {} applied.", total, applied));
                QMetaObject::invokeMethod(this, [this, rows = std::move(rows), total, applied]() mutable {
                    _totalCount = total;
                    _appliedCount = applied;
                    _migrations->setRows(std::move(rows));
                    emit modelRefreshed();
                }, Qt::QueuedConnection);
            }
            catch (std::exception const& e)
            {
                appLog("ERROR", std::format("refresh() failed: {}", e.what()));
            }
        });
    }

    // Progress-manager callbacks (invoked on the GUI thread via QMetaObject::invokeMethod)
    void onProgressUpdate(QString table, quint64 cur, quint64 tot, QString msg)
    {
        _currentTable = table;
        _operationLabel = tot > 0
                              ? QStringLiteral("%1 · %2 / %3").arg(table).arg(cur).arg(tot)
                              : QStringLiteral("%1 · %2").arg(table).arg(cur);
        emit operationLabelChanged();
        if (!msg.isEmpty())
            setStatus(msg);
    }
    void onItemsProcessed(quint64 count)
    {
        _itemsProcessed += count;
        recomputeProgress();
    }
    void onSetTotalItems(quint64 total)
    {
        _totalItems = total;
        recomputeProgress();
    }
    void onAddTotalItems(quint64 add)
    {
        _totalItems += add;
        recomputeProgress();
    }

  signals:
    void databasePathChanged();
    void pluginPathChanged();
    void odbcDsnChanged();
    void useDsnChanged();
    void usernameChanged();
    void passwordChanged();
    void credentialsNeeded(QString errorMessage);
    void statusChanged();
    void logChanged();
    void pluginLoadedChanged();
    void dbConnectedChanged();
    void modelRefreshed();
    void busyChanged();
    void operationProgressChanged();
    void operationLabelChanged();
    void schemaReady(QVariantList schema);
    void queryResultReady(QString result);
    void migrationSqlReady(QString sql);

  public slots:
    void runBackup(QString const& outputPath, bool schemaOnly)
    {
        runOperation(/*isBackup=*/true, outputPath, schemaOnly);
    }
    void runRestore(QString const& inputPath, bool schemaOnly)
    {
        runOperation(/*isBackup=*/false, inputPath, schemaOnly);
    }

  private:
    class GuiProgressManager: public Lightweight::SqlBackup::ProgressManager
    {
      public:
        explicit GuiProgressManager(Backend* owner):
            _owner { owner }
        {
        }

        void Update(Lightweight::SqlBackup::Progress const& p) override
        {
            ++_errorCount; // kept in sync only for Error states below
            if (p.state != Lightweight::SqlBackup::Progress::State::Error)
                --_errorCount;

            auto const table = QString::fromStdString(p.tableName);
            auto const cur = static_cast<quint64>(p.currentRows);
            auto const tot = static_cast<quint64>(p.totalRows.value_or(0));
            auto const msg = QString::fromStdString(p.message);
            QMetaObject::invokeMethod(
                _owner,
                [owner = _owner, table, cur, tot, msg]() {
                    owner->onProgressUpdate(table, cur, tot, msg);
                },
                Qt::QueuedConnection);
        }

        void AllDone() override
        {
            // Final completion is handled by the worker-thread lambda after the
            // Backup/Restore call returns — nothing to do here.
        }

        void OnItemsProcessed(size_t count) override
        {
            auto const c = static_cast<quint64>(count);
            QMetaObject::invokeMethod(
                _owner, [owner = _owner, c]() { owner->onItemsProcessed(c); }, Qt::QueuedConnection);
        }

        void SetTotalItems(size_t total) override
        {
            auto const t = static_cast<quint64>(total);
            QMetaObject::invokeMethod(
                _owner, [owner = _owner, t]() { owner->onSetTotalItems(t); }, Qt::QueuedConnection);
        }

        void AddTotalItems(size_t add) override
        {
            auto const a = static_cast<quint64>(add);
            QMetaObject::invokeMethod(
                _owner, [owner = _owner, a]() { owner->onAddTotalItems(a); }, Qt::QueuedConnection);
        }

        [[nodiscard]] size_t ErrorCount() const noexcept override
        {
            return _errorCount;
        }

      private:
        Backend* _owner;
        size_t _errorCount { 0 };
    };

    void runOperation(bool isBackup, QString const& path, bool schemaOnly)
    {
        if (_busy)
        {
            setStatus(QStringLiteral("An operation is already running."));
            return;
        }
        if (path.isEmpty())
        {
            setStatus(QStringLiteral("Choose a file first."));
            return;
        }

        if (_worker.joinable())
            _worker.detach();

        _itemsProcessed = 0;
        _totalItems = 0;
        _operationProgress = 0.0;
        _operationLabel = isBackup ? QStringLiteral("Preparing backup…") : QStringLiteral("Preparing restore…");
        emit operationProgressChanged();
        emit operationLabelChanged();
        setBusy(true);
        setStatus(isBackup ? QStringLiteral("Starting backup…") : QStringLiteral("Starting restore…"));

        auto const connStr = Lightweight::SqlConnection::DefaultConnectionString();
        auto const jobs = std::max(1u, std::thread::hardware_concurrency());

        _worker = std::thread([this, isBackup, path, schemaOnly, connStr, jobs]() {
            QString errMsg;
            bool ok = true;
            try
            {
                GuiProgressManager progress { this };
                if (isBackup)
                {
                    Lightweight::SqlBackup::BackupSettings s;
                    s.schemaOnly = schemaOnly;
                    Lightweight::SqlBackup::Backup(std::filesystem::path(path.toStdString()),
                                                   connStr,
                                                   jobs,
                                                   progress,
                                                   /*schema*/ std::string {},
                                                   /*filter*/ std::string { "*" },
                                                   /*retry*/ Lightweight::SqlBackup::RetrySettings {},
                                                   s);
                }
                else
                {
                    Lightweight::SqlBackup::RestoreSettings s;
                    s.schemaOnly = schemaOnly;
                    Lightweight::SqlBackup::Restore(std::filesystem::path(path.toStdString()),
                                                    connStr,
                                                    jobs,
                                                    progress,
                                                    /*schema*/ std::string {},
                                                    /*filter*/ std::string { "*" },
                                                    /*retry*/ Lightweight::SqlBackup::RetrySettings {},
                                                    s);
                }
            }
            catch (std::exception const& e)
            {
                ok = false;
                errMsg = QString::fromUtf8(e.what());
            }

            QMetaObject::invokeMethod(
                this,
                [this, isBackup, ok, errMsg]() {
                    setBusy(false);
                    _operationProgress = ok ? 1.0 : _operationProgress;
                    emit operationProgressChanged();
                    if (ok)
                    {
                        setStatus(isBackup ? QStringLiteral("Backup complete.")
                                           : QStringLiteral("Restore complete."));
                        _operationLabel = QStringLiteral("Done.");
                    }
                    else
                    {
                        setStatus((isBackup ? QStringLiteral("Backup failed: ")
                                            : QStringLiteral("Restore failed: "))
                                  + errMsg);
                        _operationLabel = QStringLiteral("Failed.");
                    }
                    emit operationLabelChanged();
                    refresh();
                },
                Qt::QueuedConnection);
        });
    }

    template <typename F>
    void runAsync(F&& fn)
    {
        if (_worker.joinable())
            _worker.detach();
        _worker = std::thread(std::forward<F>(fn));
    }

    void appLog(std::string_view level, std::string_view message)
    {
        using namespace std::chrono;
        auto const now = zoned_time { current_zone(), system_clock::now() };
        auto line = std::format("[{:%H:%M:%S}] [{:5}] {}\n", now, level, message);
        std::fputs(line.c_str(), stdout);
        auto qline = QString::fromStdString(line);
        QMetaObject::invokeMethod(this, [this, qline]() {
            _log += qline;
            emit logChanged();
        }, Qt::QueuedConnection);
    }

    void setBusy(bool b)
    {
        if (_busy != b)
        {
            _busy = b;
            emit busyChanged();
        }
    }

    void recomputeProgress()
    {
        _operationProgress = _totalItems > 0 ? std::clamp(static_cast<double>(_itemsProcessed)
                                                              / static_cast<double>(_totalItems),
                                                          0.0,
                                                          1.0)
                                             : 0.0;
        emit operationProgressChanged();
    }

    void setStatus(QString const& s)
    {
        _status = s;
        emit statusChanged();
    }

    static std::string sqliteDriver()
    {
#if defined(_WIN32) || defined(_WIN64)
        return "SQLite3 ODBC Driver";
#else
        return "SQLite3";
#endif
    }

    GuiSqlLogger _sqlLogger;
    QString _databasePath;
    QString _pluginPath;
    QString _odbcDsn;
    QString _username;
    QString _password;
    bool _useDsn { true };
    bool _dbConnected { false };
    QString _log;
    QString _status { QStringLiteral("Idle.") };
    int _totalCount { 0 };
    int _appliedCount { 0 };
    MigrationsModel* _migrations;
    std::unique_ptr<Lightweight::Tools::PluginLoader> _loader;

    // Connection + plugin worker threads (detached — never joined on GUI thread)
    std::thread _pluginWorker;
    bool _pluginWorkerRunning { false };
    bool _connectWorkerRunning { false };

    // Backup/restore state
    std::thread _worker;
    bool _busy { false };
    double _operationProgress { 0.0 };
    QString _operationLabel;
    QString _currentTable;
    quint64 _itemsProcessed { 0 };
    quint64 _totalItems { 0 };
};
