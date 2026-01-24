// SPDX-License-Identifier: Apache-2.0

#include "DataMapper/DataMapper.hpp"
#include "SqlBackup/Sha256.hpp"
#include "SqlConnection.hpp"
#include "SqlErrorDetection.hpp"
#include "SqlMigration.hpp"
#include "SqlTransaction.hpp"

#include <format>
#include <stdexcept>

namespace Lightweight::SqlMigration
{

void MigrationManager::AddMigration(MigrationBase const* migration)
{
    // Check for duplicate timestamps
    auto const timestamp = migration->GetTimestamp();
    auto const it =
        std::ranges::find_if(_migrations, [timestamp](MigrationBase const* m) { return m->GetTimestamp() == timestamp; });

    if (it != _migrations.end())
    {
        throw std::runtime_error(std::format("Duplicate migration timestamp {} detected: '{}' conflicts with '{}'",
                                             timestamp.value,
                                             migration->GetTitle(),
                                             (*it)->GetTitle()));
    }

    _migrations.emplace_back(migration);
    _migrations.sort([](MigrationBase const* a, MigrationBase const* b) { return a->GetTimestamp() < b->GetTimestamp(); });
}

MigrationManager& MigrationManager::GetInstance()
{
    static MigrationManager instance;
    return instance;
}

MigrationManager::MigrationList const& MigrationManager::GetAllMigrations() const noexcept
{
    return _migrations;
}

MigrationBase const* MigrationManager::GetMigration(MigrationTimestamp timestamp) const noexcept
{
    auto const it = std::ranges::find_if(
        _migrations, [timestamp](MigrationBase const* migration) { return migration->GetTimestamp() == timestamp; });
    return it != std::end(_migrations) ? *it : nullptr;
}

void MigrationManager::RemoveAllMigrations()
{
    _migrations.clear();
}

struct SchemaMigration
{
    Field<uint64_t, PrimaryKey::AutoAssign> version;
    Field<std::optional<SqlString<65>>> checksum; // SHA-256 hex (64 chars + null, optional for backward compatibility)
    Field<std::optional<SqlDateTime>> applied_at; // Timestamp when migration was applied

    static constexpr std::string_view TableName = "schema_migrations";
};

DataMapper& MigrationManager::GetDataMapper()
{
    if (!_dataMapper)
        _dataMapper = &DataMapper::AcquireThreadLocal();

    return *_dataMapper;
}

void MigrationManager::CloseDataMapper()
{
    _dataMapper = nullptr;
}

void MigrationManager::CreateMigrationHistory()
{
    try
    {
        GetDataMapper().CreateTable<SchemaMigration>();
    }
    catch (SqlException const& ex)
    {
        // Only ignore "table already exists" errors - re-throw any other errors
        if (!IsTableAlreadyExistsError(ex.info(), GetDataMapper().Connection().ServerType()))
        {
            throw;
        }
        // Table already exists - this is expected, continue
    }
}

std::vector<MigrationTimestamp> MigrationManager::GetAppliedMigrationIds() const
{
    auto result = std::vector<MigrationTimestamp> {};

    auto& dm = GetDataMapper();
    auto const records = dm.Query<SchemaMigration>().OrderBy("version", SqlResultOrdering::ASCENDING).All();
    for (auto const& record: records)
        result.emplace_back(MigrationTimestamp { record.version.Value() });

    return result;
}

MigrationManager::MigrationList MigrationManager::GetPending() const noexcept
{
    auto const applied = GetAppliedMigrationIds();
    MigrationList pending;
    for (auto const* migration: _migrations)
        if (std::ranges::find(applied, migration->GetTimestamp()) == std::end(applied))
            pending.push_back(migration); // TODO: filter those that weren't applied yet
    return pending;
}

void MigrationManager::ApplySingleMigration(MigrationBase const& migration)
{
    auto& dm = GetDataMapper();
    auto transaction = SqlTransaction { dm.Connection(), SqlTransactionMode::ROLLBACK };

    SqlMigrationQueryBuilder migrationBuilder = dm.Connection().Migration();
    migration.Up(migrationBuilder);

    SqlMigrationPlan const plan = std::move(migrationBuilder).GetPlan();

    auto stmt = SqlStatement { dm.Connection() };
    size_t stepIndex = 0;

    for (SqlMigrationPlanElement const& step: plan.steps)
    {
        auto const sqlScripts = ToSql(dm.Connection().QueryFormatter(), step);
        for (auto const& sqlScript: sqlScripts)
        {
            try
            {
                stmt.ExecuteDirect(sqlScript);
            }
            catch (SqlException const& ex)
            {
                throw SqlException(
                    SqlErrorInfo { .nativeErrorCode = ex.info().nativeErrorCode,
                                   .sqlState = ex.info().sqlState,
                                   .message = std::format("Migration '{}' (timestamp {}) failed at step {}: {}\nSQL: {}",
                                                          migration.GetTitle(),
                                                          migration.GetTimestamp().value,
                                                          stepIndex,
                                                          ex.info().message,
                                                          sqlScript) });
            }
        }
        ++stepIndex;
    }

    auto const checksum = migration.ComputeChecksum(dm.Connection().QueryFormatter());
    dm.CreateExplicit(SchemaMigration {
        .version = migration.GetTimestamp().value, .checksum = checksum, .applied_at = SqlDateTime::Now() });
    transaction.Commit();
}

void MigrationManager::RevertSingleMigration(MigrationBase const& migration)
{
    auto& dm = GetDataMapper();
    auto transaction = SqlTransaction { dm.Connection(), SqlTransactionMode::ROLLBACK };

    SqlMigrationQueryBuilder migrationBuilder = dm.Connection().Migration();
    migration.Down(migrationBuilder); // Use Down() to revert

    SqlMigrationPlan const plan = std::move(migrationBuilder).GetPlan();

    auto stmt = SqlStatement { dm.Connection() };
    size_t stepIndex = 0;

    for (SqlMigrationPlanElement const& step: plan.steps)
    {
        auto const sqlScripts = ToSql(dm.Connection().QueryFormatter(), step);
        for (auto const& sqlScript: sqlScripts)
        {
            try
            {
                stmt.ExecuteDirect(sqlScript);
            }
            catch (SqlException const& ex)
            {
                throw SqlException(SqlErrorInfo {
                    .nativeErrorCode = ex.info().nativeErrorCode,
                    .sqlState = ex.info().sqlState,
                    .message = std::format("Migration rollback '{}' (timestamp {}) failed at step {}: {}\nSQL: {}",
                                           migration.GetTitle(),
                                           migration.GetTimestamp().value,
                                           stepIndex,
                                           ex.info().message,
                                           sqlScript) });
            }
        }
        ++stepIndex;
    }

    dm.Query<SchemaMigration>().Where("version", "=", migration.GetTimestamp().value).Delete();
    transaction.Commit();
}

size_t MigrationManager::ApplyPendingMigrations(ExecuteCallback const& feedbackCallback)
{
    auto const pendingMigrations = GetPending();

#if !defined(__cpp_lib_ranges_enumerate)
    int index { -1 };
    for (auto& migration: pendingMigrations)
    {
        ++index;
#else
    for (auto&& [index, migration]: pendingMigrations | std::views::enumerate)
    {
#endif
        if (feedbackCallback)
            feedbackCallback(*migration, index, _migrations.size());
        ApplySingleMigration(*migration);
    }

    return pendingMigrations.size();
}

SqlTransaction MigrationManager::Transaction()
{
    return SqlTransaction { GetDataMapper().Connection() };
}

std::vector<std::string> MigrationManager::PreviewMigration(MigrationBase const& migration) const
{
    auto& dm = GetDataMapper();
    SqlMigrationQueryBuilder migrationBuilder = dm.Connection().Migration();
    migration.Up(migrationBuilder);

    SqlMigrationPlan const plan = std::move(migrationBuilder).GetPlan();
    return plan.ToSql();
}

std::vector<std::string> MigrationManager::PreviewPendingMigrations(ExecuteCallback const& feedbackCallback) const
{
    auto const pendingMigrations = GetPending();
    std::vector<std::string> allStatements;

#if !defined(__cpp_lib_ranges_enumerate)
    int index { -1 };
    for (auto const* migration: pendingMigrations)
    {
        ++index;
#else
    for (auto&& [index, migration]: pendingMigrations | std::views::enumerate)
    {
#endif
        if (feedbackCallback)
            feedbackCallback(*migration, static_cast<size_t>(index), pendingMigrations.size());

        auto statements = PreviewMigration(*migration);
        allStatements.insert(allStatements.end(), statements.begin(), statements.end());
    }

    return allStatements;
}

std::string MigrationBase::ComputeChecksum(SqlQueryFormatter const& formatter) const
{
    SqlMigrationQueryBuilder builder(formatter);
    this->Up(builder);

    auto const plan = std::move(builder).GetPlan();
    auto const statements = plan.ToSql();

    std::string combined;
    for (auto const& sql: statements)
    {
        combined += sql;
        combined += '\n';
    }

    return SqlBackup::Sha256::Hash(combined);
}

std::vector<ChecksumVerificationResult> MigrationManager::VerifyChecksums() const
{
    std::vector<ChecksumVerificationResult> results;
    auto& dm = GetDataMapper();

    // Get all applied migrations with their checksums
    auto appliedMigrations = dm.Query<SchemaMigration>().OrderBy("version", SqlResultOrdering::ASCENDING).All();

    for (auto const& record: appliedMigrations)
    {
        auto const timestamp = MigrationTimestamp { record.version.Value() };
        auto const* migration = GetMigration(timestamp);

        // Get stored checksum, converting SqlFixedString to std::string
        auto const& checksumOpt = record.checksum.Value();
        std::string storedChecksum;
        if (checksumOpt.has_value())
            storedChecksum = std::string(checksumOpt->str());

        if (!migration)
        {
            // Migration was applied but is no longer registered
            results.push_back(ChecksumVerificationResult { .timestamp = timestamp,
                                                           .title = "(Unknown Migration)",
                                                           .storedChecksum = storedChecksum,
                                                           .computedChecksum = "",
                                                           .matches = false });
            continue;
        }

        auto const computedChecksum = migration->ComputeChecksum(dm.Connection().QueryFormatter());

        // Consider it a match if:
        // 1. Both checksums exist and are equal, OR
        // 2. Stored checksum is empty (migration was applied before checksums were implemented)
        bool const matches = storedChecksum.empty() || storedChecksum == computedChecksum;

        // Only report actual mismatches (stored checksum exists but differs)
        if (!matches)
        {
            results.push_back(ChecksumVerificationResult { .timestamp = timestamp,
                                                           .title = migration->GetTitle(),
                                                           .storedChecksum = storedChecksum,
                                                           .computedChecksum = computedChecksum,
                                                           .matches = false });
        }
    }

    return results;
}

} // namespace Lightweight::SqlMigration
