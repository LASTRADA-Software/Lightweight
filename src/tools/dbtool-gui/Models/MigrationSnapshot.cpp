// SPDX-License-Identifier: Apache-2.0

#include "../ExceptionMessage.hpp"
#include "MigrationSnapshot.hpp"

#include <Lightweight/SqlMigration.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <unordered_set>

namespace DbtoolGui
{

namespace
{

    using AppliedSet = std::unordered_set<std::uint64_t>;

    /// Returns the version of the release whose range contains `timestamp`, or
    /// an empty string if `timestamp` is beyond every registered release (the
    /// "unreleased" bucket).
    QString ReleaseVersionFor(std::uint64_t timestamp,
                              std::vector<Lightweight::SqlMigration::MigrationRelease> const& releases)
    {
        // The release with the smallest `highestTimestamp` that is still `>=` the migration
        // timestamp, as `MigrationManager::GetMigrationsForRelease` decides it.
        QString best;
        auto bestTimestamp = std::numeric_limits<std::uint64_t>::max();
        for (auto const& release: releases)
        {
            if (release.highestTimestamp.value >= timestamp && release.highestTimestamp.value < bestTimestamp)
            {
                best = QString::fromStdString(release.version);
                bestTimestamp = release.highestTimestamp.value;
            }
        }
        return best;
    }

    [[nodiscard]] QString TitleOf(Lightweight::SqlMigration::MigrationBase const& migration)
    {
        auto const title = migration.GetTitle();
        return QString::fromUtf8(title.data(), static_cast<qsizetype>(title.size()));
    }

    void SortNewestFirst(std::vector<MigrationRow>& rows)
    {
        std::ranges::sort(rows, [](MigrationRow const& a, MigrationRow const& b) { return a.timestamp > b.timestamp; });
    }

    /// One row per plugin-declared migration, all with the same `status`; reads no database.
    [[nodiscard]] std::vector<MigrationRow> PluginRowsWithStatus(Lightweight::SqlMigration::MigrationManager const& manager,
                                                                 QString const& status)
    {
        auto const& registered = manager.GetAllMigrations();
        auto const& releases = manager.GetAllReleases();
        auto rows = std::vector<MigrationRow> {};
        rows.reserve(registered.size());
        for (auto const* migration: registered)
        {
            auto const timestamp = migration->GetTimestamp().value;
            rows.push_back(MigrationRow {
                .timestamp = timestamp,
                .title = TitleOf(*migration),
                .status = status,
                .releaseVersion = ReleaseVersionFor(timestamp, releases),
                .checksumMismatch = false,
                .selected = false,
            });
        }
        SortNewestFirst(rows);
        return rows;
    }

    /// The rows of the plugin-declared migrations with their applied state and checksum check, plus an
    /// "unknown" row for each applied migration no plugin declares.
    [[nodiscard]] std::vector<MigrationRow> DatabaseRows(
        Lightweight::SqlMigration::MigrationManager const& manager,
        std::vector<Lightweight::SqlMigration::MigrationTimestamp> const& applied,
        AppliedSet const& appliedSet)
    {
        auto const& registered = manager.GetAllMigrations();
        auto const& releases = manager.GetAllReleases();

        auto mismatchSet = AppliedSet {};
        for (auto const& checksum: manager.VerifyChecksums())
            if (!checksum.matches)
                mismatchSet.insert(checksum.timestamp.value);

        auto registeredSet = AppliedSet {};
        registeredSet.reserve(registered.size());
        auto rows = std::vector<MigrationRow> {};
        rows.reserve(registered.size() + applied.size());
        for (auto const* migration: registered)
        {
            auto const timestamp = migration->GetTimestamp().value;
            registeredSet.insert(timestamp);
            auto row = MigrationRow {
                .timestamp = timestamp,
                .title = TitleOf(*migration),
                .status = appliedSet.contains(timestamp) ? QStringLiteral("applied") : QStringLiteral("pending"),
                .releaseVersion = ReleaseVersionFor(timestamp, releases),
                .checksumMismatch = mismatchSet.contains(timestamp),
            };
            if (row.checksumMismatch && row.status == QStringLiteral("applied"))
                row.status = QStringLiteral("checksum-mismatch");
            rows.push_back(std::move(row));
        }

        for (auto const& timestamp: applied)
        {
            if (registeredSet.contains(timestamp.value))
                continue;
            rows.push_back(MigrationRow {
                .timestamp = timestamp.value,
                .title = QStringLiteral("(unregistered)"),
                .status = QStringLiteral("unknown"),
                .releaseVersion = ReleaseVersionFor(timestamp.value, releases),
                .checksumMismatch = false,
            });
        }
        SortNewestFirst(rows);
        return rows;
    }

    /// One row per declared release, newest first, its status taken from @p appliedSet.
    [[nodiscard]] std::vector<ReleaseRow> ReleaseRows(Lightweight::SqlMigration::MigrationManager const& manager,
                                                      AppliedSet const& appliedSet)
    {
        auto rows = std::vector<ReleaseRow> {};
        rows.reserve(manager.GetAllReleases().size());
        for (auto const& release: manager.GetAllReleases())
        {
            auto const migrations = manager.GetMigrationsForRelease(release.version);
            auto const isApplied = [&appliedSet](auto const* migration) {
                return appliedSet.contains(migration->GetTimestamp().value);
            };

            auto status = QString {};
            if (migrations.empty())
                status = QStringLiteral("empty");
            else if (std::ranges::all_of(migrations, isApplied))
                status = QStringLiteral("applied");
            else if (std::ranges::any_of(migrations, isApplied))
                status = QStringLiteral("partial");
            else
                status = QStringLiteral("pending");

            rows.push_back(ReleaseRow {
                .version = QString::fromStdString(release.version),
                .highestTimestamp = release.highestTimestamp.value,
                .migrationCount = static_cast<int>(migrations.size()),
                .status = status,
            });
        }
        // Newest release on top, where the eye lands first.
        std::ranges::sort(rows,
                          [](ReleaseRow const& a, ReleaseRow const& b) { return a.highestTimestamp > b.highestTimestamp; });
        return rows;
    }

    /// The highest declared release, or "latest" when none is declared, with ` (+N unreleased)` for the
    /// migrations above it.
    [[nodiscard]] QString TargetReleaseLabel(Lightweight::SqlMigration::MigrationManager const& manager)
    {
        auto const& releases = manager.GetAllReleases();
        auto const& allMigrations = manager.GetAllMigrations();
        if (releases.empty())
            return allMigrations.empty() ? QStringLiteral("(no migrations)") : QStringLiteral("latest");

        auto const& last = releases.back();
        auto const unreleased = std::ranges::count_if(allMigrations, [&last](auto const* migration) {
            return migration->GetTimestamp().value > last.highestTimestamp.value;
        });
        auto label = QString::fromStdString(last.version);
        if (unreleased > 0)
            label += QStringLiteral(" (+%1 unreleased)").arg(unreleased);
        return label;
    }

} // namespace

MigrationSnapshot CaptureMigrationSnapshot(Lightweight::SqlMigration::MigrationManager const& manager, AppliedState state)
{
    auto snapshot = MigrationSnapshot {};
    auto appliedSet = AppliedSet {};
    switch (state)
    {
        case AppliedState::Unknown:
            snapshot.migrations = PluginRowsWithStatus(manager, QStringLiteral("unknown"));
            break;
        case AppliedState::PluginsOnly:
            snapshot.migrations = PluginRowsWithStatus(manager, QStringLiteral("pending"));
            break;
        case AppliedState::Database:
            try
            {
                auto const applied = manager.GetAppliedMigrationIds();
                appliedSet.reserve(applied.size());
                for (auto const& timestamp: applied)
                    appliedSet.insert(timestamp.value);
                snapshot.migrations = DatabaseRows(manager, applied, appliedSet);
            }
            catch (std::exception const& e)
            {
                snapshot.warning = QStringLiteral("Could not read migration history: %1").arg(ExceptionMessage(e));
                snapshot.migrations = PluginRowsWithStatus(manager, QStringLiteral("pending"));
            }
            break;
    }
    snapshot.releases = ReleaseRows(manager, appliedSet);
    snapshot.preMigrateStepCount = static_cast<int>(manager.GetPreMigrateSteps().size());
    snapshot.targetReleaseLabel = TargetReleaseLabel(manager);
    return snapshot;
}

} // namespace DbtoolGui
