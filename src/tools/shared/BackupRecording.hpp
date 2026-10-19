// SPDX-License-Identifier: Apache-2.0
//
// Turns a finished backup or restore into a `schema_backups` record, and words the backup requirement;
// shared by dbtool and dbtool-gui.

#pragma once

#include <Lightweight/DataBinder/SqlDateTime.hpp>
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlConnectInfo.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::Tools
{

/// Why a backup, restore or waiver could not be recorded.
struct BackupRecordError
{
    /// What went wrong.
    enum class Kind : std::uint8_t
    {
        VerificationFailed, ///< The archive failed `SqlBackup::VerifyArchive`; nothing was written.
        WriteFailed,        ///< The record could not be written to (or its state read from) the database.
        PartialArchive,     ///< The archive is filtered, schema-only or lacks a failed table; nothing was written.
    };

    /// What went wrong.
    Kind kind = Kind::WriteFailed;
    /// The verification problem or the database's message.
    std::string message;
};

/// What a backup records about the moment it started reading the database.
struct BackupStart
{
    /// UTC; informational, it plays no part in whether the record covers the database.
    SqlDateTime startedAt;
    /// The migration history read before the backup read any data.
    SqlMigration::MigrationHistoryState observedState;
};

/// The tool version a record stores, e.g. `dbtool 1.2.3`.
///
/// @param toolName The tool's name.
/// @return The name and this build's version.
[[nodiscard]] std::string ToolVersion(std::string_view toolName);

/// Opens a mapper on its own connection for writing records.
///
/// @param connectionString The database to record into.
/// @param defaultSchema The schema `schema_backups` lives in; empty for the connection's default.
/// @return The connected mapper, with @p defaultSchema made the session's default where the DBMS has one.
/// @throws std::invalid_argument when @p defaultSchema is not `[A-Za-z0-9_]+`.
/// @throws SqlException when the connection fails.
[[nodiscard]] DataMapper ConnectRecordDataMapper(SqlConnectionString const& connectionString,
                                                 std::string_view defaultSchema);

/// Captures the state a backup records; call it before the backup reads any data.
///
/// @param dataMapper The mapper the record will be written with.
/// @return The start time and the migration history as it is now.
/// @throws SqlException when the history cannot be read.
[[nodiscard]] BackupStart CaptureBackupStart(DataMapper& dataMapper);

/// Verifies a written archive and records it as a backup; a partial archive is not recorded.
///
/// @param dataMapper The mapper to write the record with.
/// @param archive The archive the backup wrote.
/// @param start What `CaptureBackupStart` captured before the backup.
/// @param toolVersion The tool that wrote the archive (see `ToolVersion`).
/// @return The stored record, or why the archive was not recorded.
[[nodiscard]] std::expected<SqlMigration::BackupRecord, BackupRecordError> RecordBackup(DataMapper& dataMapper,
                                                                                        std::filesystem::path const& archive,
                                                                                        BackupStart const& start,
                                                                                        std::string_view toolVersion);

/// Verifies a restored archive and records the restore with the migration history it left; a partial
/// archive is not recorded.
///
/// @param dataMapper The mapper to write the record with.
/// @param archive The archive that was restored.
/// @param toolVersion The tool that restored it (see `ToolVersion`).
/// @return The stored record, or why the restore was not recorded.
[[nodiscard]] std::expected<SqlMigration::BackupRecord, BackupRecordError> RecordRestore(
    DataMapper& dataMapper, std::filesystem::path const& archive, std::string_view toolVersion);

/// Records that a migrating run went ahead without a required backup.
///
/// The `waived` row names no archive, holds the migration history as it is now and never satisfies a
/// requirement.
///
/// @param dataMapper The mapper to write the record with.
/// @param toolVersion The tool that ran the migrations (see `ToolVersion`).
/// @return The stored record, or the write failure.
[[nodiscard]] std::expected<SqlMigration::BackupRecord, BackupRecordError> RecordWaiver(DataMapper& dataMapper,
                                                                                        std::string_view toolVersion);

/// The message for a backup that was not recorded.
///
/// @param error Why it was not recorded.
/// @return One sentence that says the backup cannot satisfy a migration backup requirement.
[[nodiscard]] std::string DescribeUnrecordedBackup(BackupRecordError const& error);

/// The message for a restore that was not recorded.
///
/// @param error Why it was not recorded.
/// @return One sentence that says the restore cannot satisfy a migration backup requirement.
[[nodiscard]] std::string DescribeUnrecordedRestore(BackupRecordError const& error);

/// Joins reasons for a sentence: `a`, `a and b`, `a, b and c`.
///
/// @param reasons The reasons, in the order to name them.
/// @return The joined text; empty for no reasons.
[[nodiscard]] std::string JoinReasons(std::vector<std::string> const& reasons);

/// The verb that agrees with the number of reasons.
///
/// @param requirement The requirement whose reasons are named.
/// @return `requires` for one reason, else `require`.
[[nodiscard]] std::string_view RequireVerb(SqlMigration::BackupRequirement const& requirement) noexcept;

/// What a requirement demands, as the start of a sentence.
///
/// @param requirement The requirement; `required` is expected to be true.
/// @return `<reasons> require(s) a backup of this database taken after its last migration`, without a period.
[[nodiscard]] std::string DescribeBackupRequirement(SqlMigration::BackupRequirement const& requirement);

/// Why the newest backup does not satisfy a requirement.
///
/// @param record The newest `backup` or `restore` record (`BackupRequirement::lastBackup`).
/// @return `The newest backup (<file>, <date> UTC) does not match the current migration history.`
[[nodiscard]] std::string DescribeStaleBackup(SqlMigration::BackupRecord const& record);

/// How a migrating command deals with a missing backup.
enum class BackupOverride : std::uint8_t
{
    None,        ///< It refuses.
    BackupFirst, ///< It takes the backup first (dbtool's `--backup-to`).
    Waive,       ///< It migrates without one and records the waiver.
};

/// How a tool names its two overrides in the dry-run line, e.g. `--backup-to` and `--without-backup`.
struct BackupOverrideNames
{
    std::string_view backupFirst; ///< Names `BackupOverride::BackupFirst`.
    std::string_view waive;       ///< Names `BackupOverride::Waive`.
};

/// The line a dry run prints for a required backup, saying what a real run would do.
///
/// @param requirement The requirement; `required` is expected to be true.
/// @param backupOverride How the real run would deal with a missing backup.
/// @param names How the tool names its overrides.
/// @return One SQL comment line, without a trailing newline.
[[nodiscard]] std::string FormatBackupRequirementDryRun(SqlMigration::BackupRequirement const& requirement,
                                                        BackupOverride backupOverride,
                                                        BackupOverrideNames const& names);

/// The warning of a run that goes ahead without a required backup because the user waived it.
///
/// @param requirement The unsatisfied requirement.
/// @param recordProblem Why the `waived` row could not be written (see `RecordWaiver`); empty when it was.
/// @return One line, without a trailing newline, that says "recorded as waived" only when the row was written.
[[nodiscard]] std::string FormatBackupWaiver(SqlMigration::BackupRequirement const& requirement,
                                             std::string_view recordProblem);

} // namespace Lightweight::Tools
