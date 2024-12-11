// SPDX-License-Identifier: Apache-2.0

#include "DataMapper/DataMapper.hpp"
#include "SqlConnection.hpp"
#include "SqlMigration.hpp"

namespace SqlMigration
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
    Field<uint64_t, PrimaryKey::Manual> version;
    Field<std::optional<int>> fixme; // FIXME: What? We cannot run tables with just a single field? Fix this.

    static constexpr std::string_view TableName = "schema_migrations";
};

DataMapper& MigrationManager::GetDataMapper()
{
    if (!_mapper.has_value())
        _mapper = DataMapper {};

    return *_mapper;
}

void MigrationManager::CreateMigrationHistory()
{
    GetDataMapper().CreateTable<SchemaMigration>();
}

std::vector<MigrationTimestamp> MigrationManager::GetAppliedMigrationIds() const
{
    auto result = std::vector<MigrationTimestamp> {};

    auto& mapper = GetDataMapper();
    auto const records = mapper.Query<SchemaMigration>(mapper.FromTable(RecordTableName<SchemaMigration>)
                                                           .Select()
                                                           .Fields<SchemaMigration>()
                                                           .OrderBy("version", SqlResultOrdering::ASCENDING)
                                                           .All());
    for (auto const& record: records)
        result.emplace_back(MigrationTimestamp { record.version.Value() });

    return result;
}

MigrationManager::MigrationList MigrationManager::GetPending() const noexcept
{
    MigrationList pending;
    auto const applied = GetAppliedMigrationIds();
    for (auto const* migration: _migrations)
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

    auto schemaMigration = SchemaMigration {};
    schemaMigration.version = migration.GetTimestamp().value;
    mapper.CreateExplicit(schemaMigration);
}

#if 0 // TODO
void MigrationManager::ApplyPendingMigrations(
    std::function<void(MigrationBase const& /*currentMigration*/, size_t /*currentOffset*/, size_t /*total*/)> const&
        feedbackCallback)
{
    auto const pendingMigrations = GetPending();

    auto conn = SqlConnection {};

    for (auto&& [index, migration]: pendingMigrations | std::views::enumerate)
    {
        SqlMigrationExecutor migrator(conn);
        if (feedbackCallback)
            feedbackCallback(*migration, index, _migrations.size());
        migration->Execute(migrator);
    }
}
#endif

} // namespace SqlMigration
