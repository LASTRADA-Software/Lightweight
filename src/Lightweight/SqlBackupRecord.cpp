// SPDX-License-Identifier: Apache-2.0

#include "DataBinder/UnicodeConverter.hpp"
#include "DataMapper/DataMapper.hpp"
#include "SqlBackup/Sha256.hpp"
#include "SqlErrorDetection.hpp"
#include "SqlMigration.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <string_view>

namespace Lightweight::SqlMigration::detail
{

/// A `schema_backups` row. Named namespace: reflection cannot see records with internal linkage.
struct SchemaBackup
{
    Field<uint64_t, PrimaryKey::ServerSideAutoIncrement> id;
    Field<SqlDateTime> created_at;
    Field<SqlUtf16String<255>> archive_file_name;
    Field<SqlString<64>> archive_sha256;
    Field<std::optional<uint64_t>> last_migration;
    Field<SqlString<64>> migration_state_digest;
    Field<SqlString<128>> tool_version;
    Field<SqlString<16>> kind;

    static constexpr std::string_view TableName = BackupRecordTableName;
};

/// The `schema_migrations` columns the history state reads.
struct AppliedMigrationRow
{
    Field<uint64_t, PrimaryKey::Manual> version;
    Field<std::optional<SqlDateTime>> applied_at;

    static constexpr std::string_view TableName = "schema_migrations";
};

} // namespace Lightweight::SqlMigration::detail

namespace Lightweight::SqlMigration
{

namespace
{
    constexpr std::string_view BackupKindText = "backup";
    constexpr std::string_view RestoreKindText = "restore";
    constexpr std::string_view WaivedKindText = "waived";

    std::string_view KindText(BackupRecordKind kind) noexcept
    {
        switch (kind)
        {
            case BackupRecordKind::Backup:
                return BackupKindText;
            case BackupRecordKind::Restore:
                return RestoreKindText;
            case BackupRecordKind::Waived:
                return WaivedKindText;
        }
        return BackupKindText;
    }

    /// The kind a stored `kind` names; an unknown one reads as `waived`, so it never satisfies a requirement.
    BackupRecordKind KindFromText(std::string_view text) noexcept
    {
        if (text == BackupKindText)
            return BackupRecordKind::Backup;
        if (text == RestoreKindText)
            return BackupRecordKind::Restore;
        return BackupRecordKind::Waived;
    }

    std::string ToUtf8String(std::u16string_view text)
    {
        auto const utf8 = ToUtf8(text);
        return std::string { reinterpret_cast<char const*>(utf8.data()), utf8.size() };
    }

    detail::SchemaBackup ToRow(BackupRecord const& record)
    {
        auto row = detail::SchemaBackup {};
        row.created_at = record.createdAt;
        row.archive_file_name = SqlUtf16String<255> { ToUtf16(std::u8string_view {
            reinterpret_cast<char8_t const*>(record.archiveFileName.data()), record.archiveFileName.size() }) };
        row.archive_sha256 = SqlString<64> { record.archiveSha256 };
        if (record.observedState.lastAppliedMigration)
            row.last_migration = record.observedState.lastAppliedMigration->value;
        row.migration_state_digest = SqlString<64> { record.observedState.digest };
        row.tool_version = SqlString<128> { record.toolVersion };
        row.kind = SqlString<16> { KindText(record.kind) };
        return row;
    }

    BackupRecord FromRow(detail::SchemaBackup const& row)
    {
        auto record = BackupRecord {};
        record.id = row.id.Value();
        record.createdAt = row.created_at.Value();
        record.archiveFileName = ToUtf8String(row.archive_file_name.Value().ToStringView());
        record.archiveSha256 = std::string { row.archive_sha256.Value().ToStringView() };
        if (row.last_migration.Value())
            record.observedState.lastAppliedMigration = MigrationTimestamp { *row.last_migration.Value() };
        record.observedState.digest = std::string { row.migration_state_digest.Value().ToStringView() };
        record.toolVersion = std::string { row.tool_version.Value().ToStringView() };
        record.kind = KindFromText(row.kind.Value().ToStringView());
        return record;
    }
} // namespace

MigrationHistoryState ReadMigrationHistoryState(DataMapper& dataMapper)
{
    try
    {
        auto rows = dataMapper.Query<detail::AppliedMigrationRow>().All();
        std::ranges::sort(rows, {}, [](auto const& row) { return row.version.Value(); });
        auto state = MigrationHistoryState {};
        auto hasher = SqlBackup::Sha256 {};
        for (auto const& row: rows)
        {
            auto const version = MigrationTimestamp { row.version.Value() };
            auto const& appliedAt = row.applied_at.Value();
            state.lastAppliedMigration = version;
            if (appliedAt && (!state.lastAppliedAt || *state.lastAppliedAt < *appliedAt))
                state.lastAppliedAt = *appliedAt;
            auto const milliseconds =
                appliedAt
                    ? std::format(
                          "{}", std::chrono::floor<std::chrono::milliseconds>(appliedAt->value()).time_since_epoch().count())
                    : std::string {};
            hasher.Update(std::format("{}|{}\n", version.value, milliseconds));
        }
        if (!rows.empty())
            state.digest = SqlBackup::Sha256::ToHex(hasher.Finalize());
        return state;
    }
    catch (SqlException const& error)
    {
        if (IsTableNotFoundError(error.info(), dataMapper.Connection().ServerType()))
            return {};
        throw;
    }
}

std::optional<BackupRecord> ReadLatestBackupRecord(DataMapper& dataMapper)
{
    try
    {
        auto const row = dataMapper.Query<detail::SchemaBackup>()
                             .Where("kind", "=", std::string { BackupKindText })
                             .OrWhere("kind", "=", std::string { RestoreKindText })
                             .OrderBy("id", SqlResultOrdering::DESCENDING)
                             .First();
        if (!row)
            return std::nullopt;
        return FromRow(*row);
    }
    catch (SqlException const& error)
    {
        if (IsTableNotFoundError(error.info(), dataMapper.Connection().ServerType()))
            return std::nullopt;
        throw;
    }
}

std::expected<BackupRecord, SqlErrorInfo> WriteBackupRecord(DataMapper& dataMapper, BackupRecord record)
{
    auto row = ToRow(record);
    try
    {
        try
        {
            record.id = dataMapper.Create(row);
        }
        catch (SqlException const& error)
        {
            if (!IsTableNotFoundError(error.info(), dataMapper.Connection().ServerType()))
                throw;
            try
            {
                dataMapper.CreateTable<detail::SchemaBackup>();
            }
            catch (SqlException const& createError)
            {
                if (!IsTableAlreadyExistsError(createError.info(), dataMapper.Connection().ServerType()))
                    throw;
            }
            record.id = dataMapper.Create(row);
        }
        return record;
    }
    catch (SqlException const& error)
    {
        return std::unexpected { error.info() };
    }
}

bool BackupRecordCovers(BackupRecord const& record, MigrationHistoryState const& state) noexcept
{
    if (record.kind != BackupRecordKind::Backup && record.kind != BackupRecordKind::Restore)
        return false;
    return record.observedState.digest == state.digest;
}

} // namespace Lightweight::SqlMigration
