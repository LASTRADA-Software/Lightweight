// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "DataMapper/DataMapper.hpp"
#include "SqlQuery.hpp"
#include "SqlTransaction.hpp"

#include <functional>
#include <list>
#include <optional>

namespace Lightweight
{

class SqlConnection;

namespace SqlMigration
{
    class MigrationBase;

    struct MigrationTimestamp
    {
        uint64_t value {};

        constexpr bool operator==(MigrationTimestamp const& other) const noexcept
        {
            return value == other.value;
        }

        constexpr bool operator!=(MigrationTimestamp const& other) const noexcept
        {
            return !(*this == other);
        }
    };

    // Interface to be implemented by the user to execute SQL migrations
    class SqlMigrationExecutor
    {
      public:
        SqlMigrationExecutor(SqlMigrationExecutor const&) = default;
        SqlMigrationExecutor(SqlMigrationExecutor&&) = delete;
        SqlMigrationExecutor& operator=(SqlMigrationExecutor const&) = default;
        SqlMigrationExecutor& operator=(SqlMigrationExecutor&&) = delete;
        virtual ~SqlMigrationExecutor() = default;

        virtual void OnCreateTable(SqlCreateTablePlan const& createTable) = 0;
        virtual void OnAlterTable(SqlAlterTablePlan const& alterTable) = 0;

        virtual void OnDropTable(std::string_view tableName) = 0;
    };

    // Main API to use for managing SQL migrations
    class MigrationManager
    {
      public:
        using MigrationList = std::list<MigrationBase const*>;

        LIGHTWEIGHT_API static MigrationManager& GetInstance()
        {
            static MigrationManager instance;
            return instance;
        }

        LIGHTWEIGHT_API void AddMigration(MigrationBase const* migration);

        [[nodiscard]] LIGHTWEIGHT_API MigrationList const& GetAllMigrations() const noexcept;

        [[nodiscard]] LIGHTWEIGHT_API MigrationBase const* GetMigration(MigrationTimestamp timestamp) const;

        LIGHTWEIGHT_API void RemoveAllMigrations();

        [[nodiscard]] LIGHTWEIGHT_API std::list<MigrationBase const*> GetPending() const noexcept;

        using ExecuteCallback =
            std::function<void(MigrationBase const& /*migration*/, size_t /*current*/, size_t /*total*/)>;

        LIGHTWEIGHT_API void ApplySingleMigration(MigrationTimestamp timestamp);
        LIGHTWEIGHT_API void ApplySingleMigration(MigrationBase const& migration);
        LIGHTWEIGHT_API size_t ApplyPendingMigrations(ExecuteCallback const& feedbackCallback = {});

        LIGHTWEIGHT_API void CreateMigrationHistory();
        [[nodiscard]] LIGHTWEIGHT_API std::vector<MigrationTimestamp> GetAppliedMigrationIds() const;

        [[nodiscard]] LIGHTWEIGHT_API DataMapper& GetDataMapper();

        [[nodiscard]] LIGHTWEIGHT_API DataMapper& GetDataMapper() const
        {
            return const_cast<MigrationManager*>(this)->GetDataMapper();
        }

        LIGHTWEIGHT_API void CloseDataMapper();

        LIGHTWEIGHT_API SqlTransaction Transaction();

      private:
        MigrationList _migrations;
        mutable DataMapper* _dataMapper { nullptr };
    };

    // Represents a single unique SQL migration
    class MigrationBase
    {
      public:
        MigrationBase(MigrationBase const&) = default;
        MigrationBase(MigrationBase&&) = delete;
        MigrationBase& operator=(MigrationBase const&) = default;
        MigrationBase& operator=(MigrationBase&&) = delete;
        MigrationBase(MigrationTimestamp timestamp, std::string_view title):
            _timestamp { timestamp },
            _title { title }
        {
            MigrationManager::GetInstance().AddMigration(this);
        }

        virtual ~MigrationBase() = default;

        virtual void Execute(SqlMigrationQueryBuilder& planer) const = 0;

        [[nodiscard]] MigrationTimestamp GetTimestamp() const noexcept
        {
            return _timestamp;
        }

        [[nodiscard]] std::string_view GetTitle() const noexcept
        {
            return _title;
        }

      private:
        MigrationTimestamp _timestamp;
        std::string_view _title;
    };

    class Migration: public MigrationBase
    {
      public:
        Migration(MigrationTimestamp timestamp,
                  std::string_view title,
                  std::function<void(SqlMigrationQueryBuilder&)> const& plan):
            MigrationBase(timestamp, title),
            _plan { plan }
        {
        }

        void Execute(SqlMigrationQueryBuilder& planer) const override
        {
            _plan(planer);
        }

      private:
        std::function<void(SqlMigrationQueryBuilder&)> _plan;
    };

} // namespace SqlMigration

} // namespace Lightweight

#define LIGHTWEIGHT_CONCATENATE(s1, s2) s1##s2

#define LIGHTWEIGHT_MIGRATION_INSTANCE(timestamp) migration_##timestamp

// Example use:
//
// LIGHTWEIGHT_SQL_MIGRATION(std::chrono::system_clock(...), "Create table 'MyTable'")
// {
//     // ...
// }
#define LIGHTWEIGHT_SQL_MIGRATION(timestamp, description)                                                         \
    struct Migration_##timestamp: public Lightweight::SqlMigration::MigrationBase                                 \
    {                                                                                                             \
        explicit Migration_##timestamp():                                                                         \
            Lightweight::SqlMigration::MigrationBase(Lightweight::SqlMigration::MigrationTimestamp { timestamp }, \
                                                     description)                                                 \
        {                                                                                                         \
        }                                                                                                         \
                                                                                                                  \
        void Execute(Lightweight::SqlMigrationQueryBuilder& planer) const override;                               \
    };                                                                                                            \
                                                                                                                  \
    static Migration_##timestamp LIGHTWEIGHT_CONCATENATE(migration_, timestamp);                                  \
                                                                                                                  \
    void Migration_##timestamp::Execute(Lightweight::SqlMigrationQueryBuilder& plan) const
