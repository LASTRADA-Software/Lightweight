// SPDX-License-Identifier: Apache-2.0

#include "BackupRequirementSupport.hpp"
#include "ExceptionMessage.hpp"

#include <exception>
#include <string>

#include <BackupRecording.hpp>

namespace DbtoolGui
{

namespace
{

    /// What declares a backup requirement, for when it could not be evaluated: the releases of the pending
    /// migrations that require one, the pending migrations that require one themselves (unless their release
    /// is named), and the pre-migrate steps that declare one, in that order.
    QStringList DeclaredBackupRequirements(Lightweight::SqlMigration::MigrationManager const& manager)
    {
        auto pending = std::vector<Lightweight::SqlMigration::MigrationBase const*> {};
        try
        {
            for (auto const* migration: manager.GetPending())
                pending.push_back(migration);
        }
        catch (std::exception const&)
        {
            // The history cannot be read either, so every migration may be pending.
            for (auto const* migration: manager.GetAllMigrations())
                pending.push_back(migration);
        }
        auto releases = QStringList {};
        auto migrations = QStringList {};
        for (auto const* migration: pending)
        {
            if (auto const* release = manager.FindReleaseForTimestamp(migration->GetTimestamp());
                release && release->requiresBackup)
            {
                auto const reason = QStringLiteral("release %1").arg(QString::fromStdString(release->version));
                if (!releases.contains(reason))
                    releases.append(reason);
            }
            else if (migration->RequiresBackup())
                migrations.append(QStringLiteral("migration %1 \"%2\"")
                                      .arg(migration->GetTimestamp().value)
                                      .arg(QString::fromStdString(std::string { migration->GetTitle() })));
        }
        auto steps = QStringList {};
        for (auto const& step: manager.GetPreMigrateSteps())
            if (step.declaresBackupRequirement)
                steps.append(QStringLiteral("step \"%1\"").arg(QString::fromStdString(step.title)));
        return releases + migrations + steps;
    }

} // namespace

BackupRequirementState EvaluateBackupRequirement(Lightweight::SqlMigration::MigrationManager& manager,
                                                 QString const& lastRecordError)
{
    auto state = BackupRequirementState {};
    try
    {
        auto const requirement = manager.BackupRequirementFor(TimestampsOf(manager.GetPending()));
        state.required = requirement.required && !requirement.satisfied;
        if (state.required)
        {
            for (auto const& reason: requirement.reasons)
                state.reasons.append(QString::fromStdString(reason));
            if (!lastRecordError.isEmpty())
                state.note = lastRecordError;
            else if (requirement.lastBackup)
                state.note = QString::fromStdString(Lightweight::Tools::DescribeStaleBackup(*requirement.lastBackup));
        }
    }
    catch (std::exception const& e)
    {
        state.reasons = DeclaredBackupRequirements(manager);
        state.required = !state.reasons.isEmpty();
        state.note = state.required ? QStringLiteral("Could not check the backup requirement: %1").arg(ExceptionMessage(e))
                                    : QString {};
    }
    return state;
}

} // namespace DbtoolGui
