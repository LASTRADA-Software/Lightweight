// SPDX-License-Identifier: Apache-2.0

#include "MigrationListModel.hpp"

#include <Lightweight/SqlMigration.hpp>

#include <QtCore/QByteArray>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace DbtoolGui
{

namespace
{

/// Returns the version of the release whose range contains `timestamp`, or
/// an empty string if `timestamp` is beyond every registered release (the
/// "unreleased" bucket from the mockup).
QString ReleaseVersionFor(uint64_t timestamp,
                          std::vector<Lightweight::SqlMigration::MigrationRelease> const& releases)
{
    // Releases are stored in declaration order; for a given migration we pick
    // the release with the smallest `highestTimestamp` that is still `>=` the
    // migration timestamp. This mirrors `MigrationManager::GetMigrationsForRelease`
    // semantics without having to call back into the manager per-row.
    QString best;
    uint64_t bestTs = UINT64_MAX;
    for (auto const& release: releases)
    {
        if (release.highestTimestamp.value >= timestamp && release.highestTimestamp.value < bestTs)
        {
            best = QString::fromStdString(release.version);
            bestTs = release.highestTimestamp.value;
        }
    }
    return best;
}

} // namespace

MigrationListModel::MigrationListModel(QObject* parent):
    QAbstractListModel(parent)
{
}

int MigrationListModel::rowCount(QModelIndex const& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(_rows.size());
}

QVariant MigrationListModel::data(QModelIndex const& index, int role) const
{
    auto const row = index.row();
    if (row < 0 || row >= static_cast<int>(_rows.size()))
        return {};
    auto const& r = _rows[static_cast<size_t>(row)];

    switch (role)
    {
        case Qt::DisplayRole:
        case TitleRole:
            return r.title;
        case TimestampRole:
            return QString::number(r.timestamp);
        case StatusRole:
            return r.status;
        case ReleaseVersionRole:
            return r.releaseVersion;
        case ChecksumMismatchRole:
            return r.checksumMismatch;
        case SelectedRole:
            return r.selected;
        default:
            return {};
    }
}

bool MigrationListModel::setData(QModelIndex const& index, QVariant const& value, int role)
{
    auto const row = index.row();
    if (row < 0 || row >= static_cast<int>(_rows.size()))
        return false;

    if (role != SelectedRole)
        return false;

    auto& r = _rows[static_cast<size_t>(row)];
    // Only pending rows are meaningful to select — already-applied rows would
    // produce a no-op in the runner and would be confusing UX.
    if (r.status != QStringLiteral("pending"))
        return false;

    auto const next = value.toBool();
    if (r.selected == next)
        return true;
    r.selected = next;
    emit dataChanged(index, index, { SelectedRole });
    emit selectionChanged();
    return true;
}

Qt::ItemFlags MigrationListModel::flags(QModelIndex const& index) const
{
    auto const base = QAbstractListModel::flags(index);
    auto const row = index.row();
    if (row < 0 || row >= static_cast<int>(_rows.size()))
        return base;
    // Mark pending rows as user-checkable so QML's `Qt.Checked` semantics work
    // if a delegate binds against the flag set. The explicit `setData` path
    // is what the current UI uses, but this keeps third-party delegates sane.
    if (_rows[static_cast<size_t>(row)].status == QStringLiteral("pending"))
        return base | Qt::ItemIsUserCheckable;
    return base;
}

QHash<int, QByteArray> MigrationListModel::roleNames() const
{
    return {
        { TimestampRole, "timestamp" },
        { TitleRole, "title" },
        { StatusRole, "status" },
        { ReleaseVersionRole, "releaseVersion" },
        { ChecksumMismatchRole, "checksumMismatch" },
        { SelectedRole, "selected" },
    };
}

void MigrationListModel::SelectAllPending(bool selected)
{
    if (_rows.empty())
        return;
    bool changed = false;
    for (auto& r: _rows)
    {
        if (r.status == QStringLiteral("pending") && r.selected != selected)
        {
            r.selected = selected;
            changed = true;
        }
    }
    if (!changed)
        return;
    emit dataChanged(index(0), index(static_cast<int>(_rows.size()) - 1), { SelectedRole });
    emit selectionChanged();
}

QStringList MigrationListModel::SelectedTimestamps() const
{
    QStringList out;
    for (auto const& r: _rows)
        if (r.selected && r.status == QStringLiteral("pending"))
            out.push_back(QString::number(r.timestamp));
    return out;
}

int MigrationListModel::SelectedCount() const noexcept
{
    int n = 0;
    for (auto const& r: _rows)
        if (r.selected && r.status == QStringLiteral("pending"))
            ++n;
    return n;
}

bool MigrationListModel::SetRowStatusByTimestamp(uint64_t timestamp, QString const& status)
{
    for (size_t i = 0; i < _rows.size(); ++i)
    {
        if (_rows[i].timestamp != timestamp)
            continue;
        if (_rows[i].status == status)
            return true;
        _rows[i].status = status;
        // Clear selection once the row leaves the pending state — nothing
        // downstream can act on a non-pending row's selection anyway.
        if (status != QStringLiteral("pending"))
            _rows[i].selected = false;
        auto const qidx = index(static_cast<int>(i));
        emit dataChanged(qidx, qidx, { StatusRole, SelectedRole });
        emit selectionChanged();
        return true;
    }
    return false;
}

namespace
{

/// Shared body for `RefreshPluginsOnly` / `RefreshAsUnknown`: enumerates the
/// plugin-declared migrations and stages a row per entry with the caller-
/// supplied blanket `status`. Neither mode consults the DB — they differ
/// only in the label the UI shows while that is the case.
std::vector<MigrationRow> StagePluginRowsWithStatus(
    Lightweight::SqlMigration::MigrationManager const& manager,
    QString const& status)
{
    using namespace Lightweight::SqlMigration;

    std::vector<MigrationRow> staged;
    auto const& registered = manager.GetAllMigrations();
    auto const& releases = manager.GetAllReleases();
    staged.reserve(registered.size());
    for (auto const* migration: registered)
    {
        auto const ts = migration->GetTimestamp().value;
        staged.push_back(MigrationRow {
            .timestamp = ts,
            .title = QString::fromUtf8(migration->GetTitle().data(),
                                       static_cast<qsizetype>(migration->GetTitle().size())),
            .status = status,
            .releaseVersion = ReleaseVersionFor(ts, releases),
            .checksumMismatch = false,
            .selected = false,
        });
    }
    std::ranges::sort(staged, [](MigrationRow const& a, MigrationRow const& b) {
        return a.timestamp > b.timestamp;
    });
    return staged;
}

} // namespace

void MigrationListModel::RefreshPluginsOnly(Lightweight::SqlMigration::MigrationManager const& manager)
{
    // Build into a staging vector first so a throw partway through leaves
    // the model in its prior state — Qt aborts if `beginResetModel` fires
    // without a matching `endResetModel`, which is what we would get if
    // `GetAllMigrations` or `GetAllReleases` threw inside the reset block.
    auto staged = StagePluginRowsWithStatus(manager, QStringLiteral("pending"));

    beginResetModel();
    _rows = std::move(staged);
    endResetModel();
}

void MigrationListModel::RefreshAsUnknown(Lightweight::SqlMigration::MigrationManager const& manager)
{
    auto staged = StagePluginRowsWithStatus(manager, QStringLiteral("unknown"));

    beginResetModel();
    _rows = std::move(staged);
    endResetModel();
}

void MigrationListModel::Refresh(Lightweight::SqlMigration::MigrationManager const& manager)
{
    using namespace Lightweight::SqlMigration;

    // Stage the whole row list before touching the model. If any of the
    // throwing calls below (GetAppliedMigrationIds, VerifyChecksums) fire,
    // we abandon the staging vector and propagate the exception; the caller
    // is responsible for its own fallback (typically `RefreshPluginsOnly`).
    // This keeps `beginResetModel` / `endResetModel` balanced.
    auto const& registered = manager.GetAllMigrations();
    auto const applied = manager.GetAppliedMigrationIds();
    auto const& releases = manager.GetAllReleases();

    std::unordered_set<uint64_t> appliedSet;
    appliedSet.reserve(applied.size());
    for (auto const& ts: applied)
        appliedSet.insert(ts.value);

    auto const mismatches = manager.VerifyChecksums();
    std::unordered_set<uint64_t> mismatchSet;
    mismatchSet.reserve(mismatches.size());
    for (auto const& m: mismatches)
        if (!m.matches)
            mismatchSet.insert(m.timestamp.value);

    std::unordered_set<uint64_t> registeredSet;
    registeredSet.reserve(registered.size());

    std::vector<MigrationRow> staged;
    staged.reserve(registered.size() + applied.size());
    for (auto const* migration: registered)
    {
        auto const ts = migration->GetTimestamp().value;
        registeredSet.insert(ts);
        MigrationRow row {
            .timestamp = ts,
            .title = QString::fromUtf8(migration->GetTitle().data(),
                                       static_cast<qsizetype>(migration->GetTitle().size())),
            .status = appliedSet.contains(ts) ? QStringLiteral("applied") : QStringLiteral("pending"),
            .releaseVersion = ReleaseVersionFor(ts, releases),
            .checksumMismatch = mismatchSet.contains(ts),
        };
        if (row.checksumMismatch && row.status == QStringLiteral("applied"))
            row.status = QStringLiteral("checksum-mismatch");
        staged.push_back(std::move(row));
    }

    for (auto const& ts: applied)
    {
        if (registeredSet.contains(ts.value))
            continue;
        staged.push_back(MigrationRow {
            .timestamp = ts.value,
            .title = QStringLiteral("(unregistered)"),
            .status = QStringLiteral("unknown"),
            .releaseVersion = ReleaseVersionFor(ts.value, releases),
            .checksumMismatch = false,
        });
    }

    std::ranges::sort(staged, [](MigrationRow const& a, MigrationRow const& b) {
        return a.timestamp > b.timestamp;
    });

    beginResetModel();
    _rows = std::move(staged);
    endResetModel();
}

} // namespace DbtoolGui
