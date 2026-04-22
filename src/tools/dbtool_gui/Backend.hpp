// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

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

  signals:
    void databasePathChanged();
    void pluginPathChanged();
    void statusChanged();
    void pluginLoadedChanged();
    void modelRefreshed();

  private:
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
};
