// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Lightweight/SqlBackup/SqlBackup.hpp>
#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlStatement.hpp>

#include <PluginLoader.hpp>

#include <QAbstractListModel>
#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
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

class Backend: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString databasePath READ databasePath WRITE setDatabasePath NOTIFY databasePathChanged)
    Q_PROPERTY(QString pluginPath READ pluginPath WRITE setPluginPath NOTIFY pluginPathChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool pluginLoaded READ pluginLoaded NOTIFY pluginLoadedChanged)
    Q_PROPERTY(int appliedCount READ appliedCount NOTIFY modelRefreshed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY modelRefreshed)
    Q_PROPERTY(MigrationsModel* migrations READ migrations CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double operationProgress READ operationProgress NOTIFY operationProgressChanged)
    Q_PROPERTY(QString operationLabel READ operationLabel NOTIFY operationLabelChanged)

  public:
    explicit Backend(QObject* parent = nullptr):
        QObject { parent },
        _migrations { new MigrationsModel(this) }
    {
        _databasePath = QStringLiteral("database.db");
        _pluginPath = defaultPluginPath();
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

    [[nodiscard]] QString status() const
    {
        return _status;
    }
    [[nodiscard]] bool pluginLoaded() const
    {
        return _loader != nullptr;
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
        if (_worker.joinable())
            _worker.join();
    }

  public slots:
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

    void loadPlugin()
    {
        try
        {
            Lightweight::SqlConnection::SetDefaultConnectionString(Lightweight::SqlConnectionString {
                .value = std::format("DRIVER={};Database={}", sqliteDriver(), _databasePath.toStdString()),
            });

            _loader = std::make_unique<Lightweight::Tools::PluginLoader>(
                std::filesystem::path(_pluginPath.toStdString()));
            auto const acquire =
                _loader->GetFunction<Lightweight::SqlMigration::MigrationManager*()>("AcquireMigrationManager");
            if (!acquire)
                throw std::runtime_error("Plugin does not export AcquireMigrationManager");

            auto* pluginManager = acquire();
            auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
            if (pluginManager && pluginManager != &manager)
            {
                for (auto const& r: pluginManager->GetAllReleases())
                    manager.RegisterRelease(r.version, r.highestTimestamp);
                for (auto const* m: pluginManager->GetAllMigrations())
                    manager.AddMigration(m);
            }
            manager.CreateMigrationHistory();
            setStatus(QStringLiteral("Plugin loaded, database opened."));
            emit pluginLoadedChanged();
            refresh();
        }
        catch (std::exception const& e)
        {
            setStatus(QStringLiteral("Error: ") + QString::fromUtf8(e.what()));
        }
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
        try
        {
            auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
            auto const pending = manager.GetPending();
            int applied = 0;
            for (auto const* m: pending)
            {
                if (m->GetTimestamp().value > targetTimestamp)
                    break;
                manager.ApplySingleMigration(*m);
                ++applied;
            }
            setStatus(QString::fromStdString(std::format("Applied {} migration(s).", applied)));
            refresh();
        }
        catch (std::exception const& e)
        {
            setStatus(QStringLiteral("Error: ") + QString::fromUtf8(e.what()));
            refresh();
        }
    }

    QVariantList databaseSchema()
    {
        try
        {
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
            setStatus(QString::fromStdString(std::format("Loaded schema: {} table(s).", out.size())));
            return out;
        }
        catch (std::exception const& e)
        {
            setStatus(QStringLiteral("Schema error: ") + QString::fromUtf8(e.what()));
            return {};
        }
    }

    QString executeQuery(QString const& sqlText)
    {
        auto const trimmed = sqlText.trimmed();
        if (trimmed.isEmpty())
            return QStringLiteral("(empty query)");
        try
        {
            Lightweight::SqlConnection conn;
            Lightweight::SqlStatement stmt { conn };
            auto cursor = stmt.ExecuteDirect(trimmed.toStdString());
            SQLHSTMT const hstmt = stmt.NativeHandle();
            auto const ncols = cursor.NumColumnsAffected();

            if (ncols == 0)
            {
                return QString::fromStdString(
                    std::format("OK — {} row(s) affected.", cursor.NumRowsAffected()));
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
                    widths[i] = std::max<int>(widths[i], row[i].size());

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
            return out;
        }
        catch (std::exception const& e)
        {
            return QStringLiteral("Error: ") + QString::fromUtf8(e.what());
        }
    }

    QString migrationSql(quint64 timestamp)
    {
        if (!_loader)
            return QStringLiteral("— Load a plugin first —");
        try
        {
            auto const& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
            auto const* migration =
                manager.GetMigration(Lightweight::SqlMigration::MigrationTimestamp { timestamp });
            if (!migration)
                return QStringLiteral("Migration not found.");

            Lightweight::SqlConnection conn;
            auto const& formatter = conn.QueryFormatter();
            Lightweight::SqlMigrationQueryBuilder builder { formatter };
            migration->Up(builder);
            auto const plan = std::move(builder).GetPlan();
            auto const statements = plan.ToSql();

            QString result;
            for (auto const& stmt: statements)
                result += QString::fromStdString(stmt) + QStringLiteral(";\n\n");
            return result.isEmpty() ? QStringLiteral("(no SQL statements generated)") : result.trimmed();
        }
        catch (std::exception const& e)
        {
            return QStringLiteral("Error: ") + QString::fromUtf8(e.what());
        }
    }

    void refresh()
    {
        if (!_loader)
            return;
        auto& manager = Lightweight::SqlMigration::MigrationManager::GetInstance();
        auto const appliedIds = manager.GetAppliedMigrationIds();
        auto const& all = manager.GetAllMigrations();
        std::vector<MigrationRow> rows;
        rows.reserve(all.size());
        for (auto const* m: all)
        {
            auto const ts = m->GetTimestamp().value;
            bool const isApplied = std::ranges::any_of(appliedIds, [&](auto id) { return id.value == ts; });
            rows.push_back(
                MigrationRow { .timestamp = ts, .title = QString::fromUtf8(m->GetTitle()), .applied = isApplied });
        }
        _totalCount = static_cast<int>(rows.size());
        _appliedCount = static_cast<int>(appliedIds.size());
        _migrations->setRows(std::move(rows));
        emit modelRefreshed();
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
    void statusChanged();
    void pluginLoadedChanged();
    void modelRefreshed();
    void busyChanged();
    void operationProgressChanged();
    void operationLabelChanged();

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
            _worker.join();

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

    static QString defaultPluginPath()
    {
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins"));
    }

    QString _databasePath;
    QString _pluginPath;
    QString _status { QStringLiteral("Idle.") };
    int _totalCount { 0 };
    int _appliedCount { 0 };
    MigrationsModel* _migrations;
    std::unique_ptr<Lightweight::Tools::PluginLoader> _loader;

    // Backup/restore state
    std::thread _worker;
    bool _busy { false };
    double _operationProgress { 0.0 };
    QString _operationLabel;
    QString _currentTable;
    quint64 _itemsProcessed { 0 };
    quint64 _totalItems { 0 };
};
