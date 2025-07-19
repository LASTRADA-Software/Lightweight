// SPDX-License-Identifier: Apache-2.0

#include "DataMapper/DataMapper.hpp"
#include "SqlConnection.hpp"
#include "SqlMigration.hpp"
#include "SqlTransaction.hpp"

namespace Lightweight::SqlMigration
{

void MigrationManager::AddMigration(MigrationBase const* migration)
{
    _migrations.emplace_back(migration);
}

MigrationManager::MigrationList const& MigrationManager::GetAllMigrations() const noexcept
{
    return _migrations;
}

MigrationBase const* MigrationManager::GetMigration(MigrationTimestamp timestamp) const
{
    auto const it = std::ranges::find_if(_migrations, [timestamp](MigrationBase const* migration) {
        return migration->GetTimestamp().value == timestamp.value;
    });
    return it != std::end(_migrations) ? *it : nullptr;
}

void MigrationManager::RemoveAllMigrations()
{
    _migrations.clear();
}

struct SchemaMigration
{
    Field<uint64_t, PrimaryKey::AutoAssign> version;

    static constexpr std::string_view TableName = "schema_migrations";
};

DataMapper& MigrationManager::GetDataMapper()
{
    if (!_mapper.has_value())
        _mapper = DataMapper {};

    return *_mapper;
}

void MigrationManager::CloseDataMapper()
{
    _mapper.reset();
}

void MigrationManager::CreateMigrationHistory()
{
    GetDataMapper().CreateTable<SchemaMigration>();
}

std::vector<MigrationTimestamp> MigrationManager::GetAppliedMigrationIds() const
{
    auto result = std::vector<MigrationTimestamp> {};

    auto& mapper = GetDataMapper();
    auto const records = mapper.Query<SchemaMigration>().OrderBy("version", SqlResultOrdering::ASCENDING).All();
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

void MigrationManager::ApplySingleMigration(MigrationTimestamp timestamp)
{
    if (MigrationBase const* migration = GetMigration(timestamp); migration)
        ApplySingleMigration(*migration);
}

void MigrationManager::ApplySingleMigration(MigrationBase const& migration)
{
    auto& mapper = GetDataMapper();
    SqlMigrationQueryBuilder migrationBuilder = mapper.Connection().Migration();
    migration.Execute(migrationBuilder);

    SqlMigrationPlan const plan = migrationBuilder.GetPlan();

    auto stmt = SqlStatement { mapper.Connection() };

    for (SqlMigrationPlanElement const& step: plan.steps)
    {
        auto const sqlScripts = ToSql(mapper.Connection().QueryFormatter(), step);
        for (auto const& sqlScript: sqlScripts)
            stmt.ExecuteDirect(sqlScript);
    }

    mapper.CreateExplicit(SchemaMigration { .version = migration.GetTimestamp().value });
}

size_t MigrationManager::ApplyPendingMigrations(ExecuteCallback const& feedbackCallback)
{
    auto const pendingMigrations = GetPending();

    for (auto&& [index, migration]: pendingMigrations | std::views::enumerate)
    {
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

} // namespace Lightweight::SqlMigration
