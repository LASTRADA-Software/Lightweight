// SPDX-License-Identifier: Apache-2.0

#include "BackupRequirementSupport.hpp"
#include "ExceptionMessage.hpp"

#include <Lightweight/SqlBackup.hpp>

#include <exception>
#include <expected>
#include <filesystem>
#include <optional>
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

    /// Default `Recorder::captureStart`: the migration history, read on a mapper of its own.
    Lightweight::Tools::BackupStart CaptureBackupStartOn(std::string const& connectionString, std::string const& schema)
    {
        auto mapper =
            Lightweight::Tools::ConnectRecordDataMapper(Lightweight::SqlConnectionString { connectionString }, schema);
        return Lightweight::Tools::CaptureBackupStart(mapper);
    }

    /// Default `Recorder::record`: verifies the archive and writes its record on a mapper of its own.
    std::expected<Lightweight::SqlMigration::BackupRecord, Lightweight::Tools::BackupRecordError> RecordRunOn(
        BackupRunner::Operation operation,
        std::filesystem::path const& archive,
        std::string const& connectionString,
        std::string const& schema,
        Lightweight::Tools::BackupStart const& start)
    {
        try
        {
            auto mapper =
                Lightweight::Tools::ConnectRecordDataMapper(Lightweight::SqlConnectionString { connectionString }, schema);
            auto const toolVersion = Lightweight::Tools::ToolVersion(RecordToolName);
            return operation == BackupRunner::Operation::Backup
                       ? Lightweight::Tools::RecordBackup(mapper, archive, start, toolVersion)
                       : Lightweight::Tools::RecordRestore(mapper, archive, toolVersion);
        }
        catch (std::exception const& e)
        {
            return std::unexpected(Lightweight::Tools::BackupRecordError {
                .kind = Lightweight::Tools::BackupRecordError::Kind::WriteFailed, .message = e.what() });
        }
    }

    /// Default `Recorder::verifyArchive`: `SqlBackup::VerifyArchive`. The record verifies the archive again.
    std::optional<Lightweight::Tools::BackupRecordError> VerifyArchiveOn(std::filesystem::path const& archive)
    {
        if (auto const verification = Lightweight::SqlBackup::VerifyArchive(archive); !verification)
            return Lightweight::Tools::BackupRecordError {
                .kind = Lightweight::Tools::BackupRecordError::Kind::VerificationFailed, .message = verification.error()
            };
        return std::nullopt;
    }

} // namespace

BackupRunner::Recorder DefaultBackupRecorder()
{
    return { .captureStart = &CaptureBackupStartOn, .verifyArchive = &VerifyArchiveOn, .record = &RecordRunOn };
}

QString DescribeUnrecorded(BackupRunner::Operation operation, Lightweight::Tools::BackupRecordError const& error)
{
    return QString::fromStdString(operation == BackupRunner::Operation::Backup
                                      ? Lightweight::Tools::DescribeUnrecordedBackup(error)
                                      : Lightweight::Tools::DescribeUnrecordedRestore(error));
}

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
