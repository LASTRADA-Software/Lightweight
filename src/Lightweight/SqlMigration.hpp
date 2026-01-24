// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "DataMapper/DataMapper.hpp"
#include "SqlQuery/Migrate.hpp"
#include "SqlTransaction.hpp"

#include <functional>
#include <list>

namespace Lightweight
{

class SqlConnection;

namespace SqlMigration
{
    class MigrationBase;

    /// Represents a unique timestamp of a migration.
    ///
    /// This struct is used to identify migrations and is used as a key in the migration history table.
    ///
    /// Note, a recommended format for the timestamp is a human readable format like YYYYMMDDHHMMSS
    ///
    /// @code
    /// MigrationTimestamp { 2026'01'17'00'31'20 };
    /// @endcode
    ///
    /// @ingroup SqlMigration
    struct MigrationTimestamp
    {
        uint64_t value {};

        constexpr std::weak_ordering operator<=>(MigrationTimestamp const& other) const noexcept = default;
    };

    /// Result of verifying a migration's checksum.
    ///
    /// @ingroup SqlMigration
    struct ChecksumVerificationResult
    {
        MigrationTimestamp timestamp;
        std::string_view title;
        std::string storedChecksum;
        std::string computedChecksum;
        bool matches;
    };

    /// Main API to use for managing SQL migrations
    ///
    /// This class is a singleton and can be accessed using the GetInstance() method.
    ///
    /// @ingroup SqlMigration
    class MigrationManager
    {
      public:
        using MigrationList = std::list<MigrationBase const*>;

        /// Get the singleton instance of the migration manager.
        ///
        /// @return Reference to the migration manager.
        /// Get the singleton instance of the migration manager.
        ///
        /// @return Reference to the migration manager.
        LIGHTWEIGHT_API static MigrationManager& GetInstance();

        /// Add a migration to the manager.
        ///
        /// @param migration Pointer to the migration to add.
        LIGHTWEIGHT_API void AddMigration(MigrationBase const* migration);

        /// Get all migrations that have been added to the manager.
        ///
        /// @return List of migrations.
        [[nodiscard]] LIGHTWEIGHT_API MigrationList const& GetAllMigrations() const noexcept;

        /// Get a migration by timestamp.
        ///
        /// @param timestamp Timestamp of the migration to get.
        /// @return Pointer to the migration if found, nullptr otherwise.
        [[nodiscard]] LIGHTWEIGHT_API MigrationBase const* GetMigration(MigrationTimestamp timestamp) const noexcept;

        /// Remove all migrations from the manager.
        ///
        /// This function is useful if the migration manager should be reset.
        LIGHTWEIGHT_API void RemoveAllMigrations();

        /// Get all migrations that have not been applied yet.
        ///
        /// @return List of pending migrations.
        [[nodiscard]] LIGHTWEIGHT_API std::list<MigrationBase const*> GetPending() const noexcept;

        using ExecuteCallback =
            std::function<void(MigrationBase const& /*migration*/, size_t /*current*/, size_t /*total*/)>;

        /// Apply a single migration from a migration object.
        ///
        /// @param migration Pointer to the migration to apply.
        LIGHTWEIGHT_API void ApplySingleMigration(MigrationBase const& migration);

        /// Revert a single migration from a migration object.
        ///
        /// @param migration Pointer to the migration to revert.
        LIGHTWEIGHT_API void RevertSingleMigration(MigrationBase const& migration);

        /// Apply all migrations that have not been applied yet.
        ///
        /// @param feedbackCallback Callback to be called for each migration.
        /// @return Number of applied migrations.
        LIGHTWEIGHT_API size_t ApplyPendingMigrations(ExecuteCallback const& feedbackCallback = {});

        /// Create the migration history table if it does not exist.
        LIGHTWEIGHT_API void CreateMigrationHistory();

        /// Get all applied migration IDs.
        ///
        /// @return Vector of applied migration IDs.
        [[nodiscard]] LIGHTWEIGHT_API std::vector<MigrationTimestamp> GetAppliedMigrationIds() const;

        /// Get the data mapper used for migrations.
        [[nodiscard]] LIGHTWEIGHT_API DataMapper& GetDataMapper();

        /// Get the data mapper used for migrations.
        [[nodiscard]] LIGHTWEIGHT_API DataMapper& GetDataMapper() const
        {
            return const_cast<MigrationManager*>(this)->GetDataMapper();
        }

        /// Close the data mapper.
        ///
        /// This function is useful if explicitly closing the connection is desired before
        /// the migration manager is destroyed.
        LIGHTWEIGHT_API void CloseDataMapper();

        /// Get a transaction for the data mapper.
        ///
        /// @return Transaction.
        LIGHTWEIGHT_API SqlTransaction Transaction();

        /// Preview SQL statements for a single migration without executing.
        ///
        /// This is useful for dry-run mode to see what SQL would be executed.
        ///
        /// @param migration The migration to preview.
        /// @return Vector of SQL statements that would be executed.
        [[nodiscard]] LIGHTWEIGHT_API std::vector<std::string> PreviewMigration(MigrationBase const& migration) const;

        /// Preview SQL statements for all pending migrations without executing.
        ///
        /// This is useful for dry-run mode to see what SQL would be executed.
        ///
        /// @param feedbackCallback Optional callback to be called for each migration.
        /// @return Vector of all SQL statements that would be executed.
        [[nodiscard]] LIGHTWEIGHT_API std::vector<std::string> PreviewPendingMigrations(
            ExecuteCallback const& feedbackCallback = {}) const;

        /// Verify checksums of all applied migrations.
        ///
        /// Compares the stored checksums in the database with the computed checksums
        /// of the current migration definitions. This helps detect if migrations
        /// have been modified after they were applied.
        ///
        /// @return Vector of verification results for migrations with mismatched or missing checksums.
        [[nodiscard]] LIGHTWEIGHT_API std::vector<ChecksumVerificationResult> VerifyChecksums() const;

      private:
        MigrationList _migrations;
        mutable DataMapper* _dataMapper { nullptr };
    };

/// Requires the user to call LIGHTWEIGHT_MIGRATION_PLUGIN() in exactly one CPP file of the migration plugin.
///
/// @ingroup SqlMigration
#define LIGHTWEIGHT_MIGRATION_PLUGIN()                                                                   \
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */                                                     \
    extern "C" LIGHTWEIGHT_EXPORT Lightweight::SqlMigration::MigrationManager* AcquireMigrationManager() \
    {                                                                                                    \
        return &Lightweight::SqlMigration::MigrationManager::GetInstance();                              \
    }

    /// Represents a single unique SQL migration.
    ///
    /// @ingroup SqlMigration
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

        /// Apply the migration.
        ///
        /// @param plan Query builder to use for building the migration plan.
        virtual void Up(SqlMigrationQueryBuilder& plan) const = 0;

        /// Revert the migration.
        ///
        /// @param plan Query builder to use for building the migration plan.
        virtual void Down(SqlMigrationQueryBuilder& /*plan*/) const {}

        /// Get the timestamp of the migration.
        ///
        /// @return Timestamp of the migration.
        [[nodiscard]] MigrationTimestamp GetTimestamp() const noexcept
        {
            return _timestamp;
        }

        /// Get the title of the migration.
        ///
        /// @return Title of the migration.
        [[nodiscard]] std::string_view GetTitle() const noexcept
        {
            return _title;
        }

        /// Compute SHA-256 checksum of migration's Up() SQL statements.
        ///
        /// The checksum is computed from the SQL statements that would be executed
        /// by this migration. This allows detecting if a migration has been modified
        /// after it was applied.
        ///
        /// @param formatter The SQL query formatter to use for generating SQL.
        /// @return SHA-256 hex string (64 characters).
        [[nodiscard]] LIGHTWEIGHT_API std::string ComputeChecksum(SqlQueryFormatter const& formatter) const;

      private:
        MigrationTimestamp _timestamp;
        std::string_view _title;
    };

    /// Represents a single unique SQL migration.
    ///
    /// This class is a convenience class that can be used to create a migration.
    ///
    /// @ingroup SqlMigration
    class Migration: public MigrationBase
    {
      public:
        /// Create a new migration.
        ///
        /// @param timestamp Timestamp of the migration.
        /// @param title Title of the migration.
        /// @param up Function to apply the migration.
        /// @param down Function to revert the migration (optional).
        Migration(MigrationTimestamp timestamp,
                  std::string_view title,
                  std::function<void(SqlMigrationQueryBuilder&)> const& up,
                  std::function<void(SqlMigrationQueryBuilder&)> const& down = {}):
            MigrationBase(timestamp, title),
            _up { up },
            _down { down }
        {
        }

        /// Apply the migration.
        ///
        /// @param builder Query builder to use for building the migration plan.
        void Up(SqlMigrationQueryBuilder& builder) const override
        {
            _up(builder);
        }

        /// Revert the migration.
        ///
        /// @param builder Query builder to use for building the migration plan.
        void Down(SqlMigrationQueryBuilder& builder) const override
        {
            if (_down)
                _down(builder);
        }

      private:
        std::function<void(SqlMigrationQueryBuilder&)> _up;
        std::function<void(SqlMigrationQueryBuilder&)> _down;
    };

} // namespace SqlMigration

} // namespace Lightweight

#define _LIGHTWEIGHT_CONCATENATE(s1, s2) s1##s2

/// @brief Represents the C++ migration object for a given timestamped migration.
/// @param timestamp Timestamp of the migration.
///
/// @ingroup SqlMigration
#define LIGHTWEIGHT_MIGRATION_INSTANCE(timestamp) migration_##timestamp

/// @brief Creates a new migration.
/// @param timestamp Timestamp of the migration.
/// @param description Description of the migration.
///
/// @note The migration will be registered with the migration manager automatically.
///
/// @code
/// #include <Lightweight/SqlMigration.hpp>
///
/// LIGHTWEIGHT_SQL_MIGRATION(20260117234120, "Create table 'MyTable'")
/// {
///     // Use 'plan' to define the migration steps, for example creating tables.
/// }
/// @endcode
///
/// @see Lightweight::SqlMigrationQueryBuilder
///
/// @ingroup SqlMigration
#define LIGHTWEIGHT_SQL_MIGRATION(timestamp, description)                                                         \
    struct Migration_##timestamp: public Lightweight::SqlMigration::MigrationBase                                 \
    {                                                                                                             \
        explicit Migration_##timestamp():                                                                         \
            Lightweight::SqlMigration::MigrationBase(Lightweight::SqlMigration::MigrationTimestamp { timestamp }, \
                                                     description)                                                 \
        {                                                                                                         \
        }                                                                                                         \
                                                                                                                  \
        void Up(Lightweight::SqlMigrationQueryBuilder& plan) const override;                                      \
        void Down(Lightweight::SqlMigrationQueryBuilder& /*plan*/) const override {}                              \
    };                                                                                                            \
                                                                                                                  \
    static Migration_##timestamp _LIGHTWEIGHT_CONCATENATE(migration_, timestamp);                                 \
                                                                                                                  \
    void Migration_##timestamp::Up(Lightweight::SqlMigrationQueryBuilder& plan) const
