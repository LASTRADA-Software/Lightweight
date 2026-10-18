// SPDX-License-Identifier: Apache-2.0

#include "BackupRecording.hpp"

#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlQueryFormatter.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Zip/ZipPath.hpp>

#include <chrono>
#include <cstddef>
#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace Lightweight::Tools
{

namespace
{
    /// Verifies @p archive and returns its SHA-256; a partial archive is refused, since it cannot cover.
    [[nodiscard]] std::expected<std::string, BackupRecordError> VerifiedArchiveSha256(std::filesystem::path const& archive)
    {
        auto verification = SqlBackup::VerifyArchive(archive);
        if (!verification)
            return std::unexpected { BackupRecordError { .kind = BackupRecordError::Kind::VerificationFailed,
                                                         .message = std::move(verification.error()) } };
        if (!verification->complete)
            return std::unexpected { BackupRecordError {
                .kind = BackupRecordError::Kind::PartialArchive,
                .message = "the archive is partial (filtered, schema-only, or a table failed)" } };
        return std::move(verification->archiveSha256);
    }

    /// The message for an unrecorded @p operation (`backup` or `restore`).
    [[nodiscard]] std::string DescribeUnrecorded(BackupRecordError const& error, std::string_view operation)
    {
        switch (error.kind)
        {
            case BackupRecordError::Kind::VerificationFailed:
                return std::format(
                    "The archive failed verification: {}; the {} is not recorded and cannot satisfy a migration backup "
                    "requirement.",
                    error.message,
                    operation);
            case BackupRecordError::Kind::PartialArchive:
                return std::format("Not recorded in {}: {}; the {} cannot satisfy a migration backup requirement.",
                                   SqlMigration::BackupRecordTableName,
                                   error.message,
                                   operation);
            case BackupRecordError::Kind::WriteFailed:
                break;
        }
        return std::format("The {} could not be recorded in {} ({}); it cannot satisfy a migration backup requirement.",
                           operation,
                           SqlMigration::BackupRecordTableName,
                           error.message);
    }

    /// Writes @p record, turning a database error into a `WriteFailed` error.
    [[nodiscard]] std::expected<SqlMigration::BackupRecord, BackupRecordError> Write(DataMapper& dataMapper,
                                                                                     SqlMigration::BackupRecord record)
    {
        auto written = SqlMigration::WriteBackupRecord(dataMapper, std::move(record));
        if (!written)
            return std::unexpected { BackupRecordError { .kind = BackupRecordError::Kind::WriteFailed,
                                                         .message = std::move(written.error().message) } };
        return std::move(*written);
    }

    /// Now, in UTC: `system_clock` is Unix time. `SqlDateTime::NowUTC` is missing in reflection builds.
    [[nodiscard]] SqlDateTime NowUtc() noexcept
    {
        return SqlDateTime { std::chrono::system_clock::now() };
    }

    /// Reads the migration history, turning a database error into a `WriteFailed` error.
    [[nodiscard]] std::expected<SqlMigration::MigrationHistoryState, BackupRecordError> ReadState(DataMapper& dataMapper)
    {
        try
        {
            return SqlMigration::ReadMigrationHistoryState(dataMapper);
        }
        catch (SqlException const& error)
        {
            return std::unexpected { BackupRecordError { .kind = BackupRecordError::Kind::WriteFailed,
                                                         .message = error.info().message } };
        }
    }
} // namespace

std::string ToolVersion(std::string_view toolName)
{
    return std::format("{} {}", toolName, LIGHTWEIGHT_TOOLS_VERSION);
}

DataMapper ConnectRecordDataMapper(SqlConnectionString const& connectionString, std::string_view defaultSchema)
{
    if (!defaultSchema.empty() && !SqlMigration::IsValidSchemaName(defaultSchema))
        throw std::invalid_argument(
            std::format("invalid schema name '{}' for {}", defaultSchema, SqlMigration::BackupRecordTableName));

    auto dataMapper = DataMapper { connectionString };
    if (!defaultSchema.empty())
    {
        if (auto const statement = dataMapper.Connection().QueryFormatter().SetDefaultSchemaStatement(defaultSchema);
            !statement.empty())
        {
            auto stmt = SqlStatement { dataMapper.Connection() };
            (void) stmt.ExecuteDirect(statement);
        }
    }
    return dataMapper;
}

BackupStart CaptureBackupStart(DataMapper& dataMapper)
{
    return BackupStart { .startedAt = NowUtc(), .observedState = SqlMigration::ReadMigrationHistoryState(dataMapper) };
}

std::expected<SqlMigration::BackupRecord, BackupRecordError> RecordBackup(DataMapper& dataMapper,
                                                                          std::filesystem::path const& archive,
                                                                          BackupStart const& start,
                                                                          std::string_view toolVersion)
{
    auto sha256 = VerifiedArchiveSha256(archive);
    if (!sha256)
        return std::unexpected { std::move(sha256.error()) };
    return Write(dataMapper,
                 SqlMigration::BackupRecord { .id = 0,
                                              .createdAt = start.startedAt,
                                              .archiveFileName = Zip::ZipPathUtf8(archive.filename()),
                                              .archiveSha256 = std::move(*sha256),
                                              .observedState = start.observedState,
                                              .toolVersion = std::string { toolVersion },
                                              .kind = SqlMigration::BackupRecordKind::Backup });
}

std::expected<SqlMigration::BackupRecord, BackupRecordError> RecordRestore(DataMapper& dataMapper,
                                                                           std::filesystem::path const& archive,
                                                                           std::string_view toolVersion)
{
    auto sha256 = VerifiedArchiveSha256(archive);
    if (!sha256)
        return std::unexpected { std::move(sha256.error()) };
    auto state = ReadState(dataMapper);
    if (!state)
        return std::unexpected { std::move(state.error()) };
    return Write(dataMapper,
                 SqlMigration::BackupRecord { .id = 0,
                                              .createdAt = NowUtc(),
                                              .archiveFileName = Zip::ZipPathUtf8(archive.filename()),
                                              .archiveSha256 = std::move(*sha256),
                                              .observedState = std::move(*state),
                                              .toolVersion = std::string { toolVersion },
                                              .kind = SqlMigration::BackupRecordKind::Restore });
}

std::expected<SqlMigration::BackupRecord, BackupRecordError> RecordWaiver(DataMapper& dataMapper,
                                                                          std::string_view toolVersion)
{
    auto state = ReadState(dataMapper);
    if (!state)
        return std::unexpected { std::move(state.error()) };
    return Write(dataMapper,
                 SqlMigration::BackupRecord { .id = 0,
                                              .createdAt = NowUtc(),
                                              .archiveFileName = {},
                                              .archiveSha256 = {},
                                              .observedState = std::move(*state),
                                              .toolVersion = std::string { toolVersion },
                                              .kind = SqlMigration::BackupRecordKind::Waived });
}

std::string DescribeUnrecordedBackup(BackupRecordError const& error)
{
    return DescribeUnrecorded(error, "backup");
}

std::string DescribeUnrecordedRestore(BackupRecordError const& error)
{
    return DescribeUnrecorded(error, "restore");
}

std::string JoinReasons(std::vector<std::string> const& reasons)
{
    auto joined = std::string {};
    // An index loop: libc++ lacks views::enumerate.
    for (auto const position: std::views::iota(std::size_t { 0 }, reasons.size()))
    {
        if (position > 0)
            joined += position + 1 == reasons.size() ? " and " : ", ";
        joined += reasons[position];
    }
    return joined;
}

std::string_view RequireVerb(SqlMigration::BackupRequirement const& requirement) noexcept
{
    return requirement.reasons.size() == 1 ? "requires" : "require";
}

std::string DescribeBackupRequirement(SqlMigration::BackupRequirement const& requirement)
{
    return std::format("{} {} a backup of this database taken after its last migration",
                       JoinReasons(requirement.reasons),
                       RequireVerb(requirement));
}

std::string DescribeStaleBackup(SqlMigration::BackupRecord const& record)
{
    return std::format("The newest backup ({}, {:%Y-%m-%d %H:%M:%S} UTC) does not match the current migration history.",
                       record.archiveFileName,
                       std::chrono::floor<std::chrono::seconds>(record.createdAt.value()));
}

std::string FormatBackupRequirementDryRun(SqlMigration::BackupRequirement const& requirement,
                                          BackupOverride backupOverride,
                                          BackupOverrideNames const& names)
{
    auto const outcome = [&] {
        if (requirement.satisfied)
            return std::string { "the newest backup covers it" };
        switch (backupOverride)
        {
            case BackupOverride::BackupFirst:
                return std::format("none covers it yet, so a real run backs up first ({})", names.backupFirst);
            case BackupOverride::Waive:
                return std::format("none covers it, so a real run migrates without one and records the waiver ({})",
                                   names.waive);
            case BackupOverride::None:
                break;
        }
        return std::string { "none covers it, so a real run refuses" };
    }();
    return std::format("-- Backup required: {}; {}.", DescribeBackupRequirement(requirement), outcome);
}

std::string FormatBackupWaiver(SqlMigration::BackupRequirement const& requirement, std::string_view recordProblem)
{
    if (recordProblem.empty())
        return std::format("Migrating without a backup although {} {} one; recorded as waived in {}.",
                           JoinReasons(requirement.reasons),
                           RequireVerb(requirement),
                           SqlMigration::BackupRecordTableName);
    return std::format("Migrating without a backup although {} {} one; the waiver could not be recorded in {} ({}).",
                       JoinReasons(requirement.reasons),
                       RequireVerb(requirement),
                       SqlMigration::BackupRecordTableName,
                       recordProblem);
}

} // namespace Lightweight::Tools
