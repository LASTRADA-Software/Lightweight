// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "DataMapper/DataMapper.hpp"
#include "SqlError.hpp"
#include "SqlQuery/Migrate.hpp"
#include "SqlQuery/MigrationPlan.hpp"
#include "SqlSchema.hpp"
#include "SqlTransaction.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight
{

class SqlConnection;

/// @defgroup SqlMigration SQL Migration
/// @brief Classes and functions for SQL schema migrations.

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
        /// The numeric timestamp value identifying the migration.
        uint64_t value {};

        /// Three-way comparison operator.
        constexpr std::weak_ordering operator<=>(MigrationTimestamp const& other) const noexcept = default;
    };

    /// Exception thrown when applying or reverting a single migration fails.
    ///
    /// Carries structured diagnostic context so callers (CLI, GUI) can render
    /// the *which migration*, *which step*, *which SQL statement* and the
    /// underlying driver error as separate fields instead of parsing one
    /// opaque message string.
    ///
    /// @ingroup SqlMigration
    class LIGHTWEIGHT_API MigrationException: public SqlException
    {
      public:
        /// Whether the failure happened while applying (Up) or reverting (Down).
        enum class Operation : std::uint8_t
        {
            Apply,
            Revert,
        };

        /// Constructs a migration exception that wraps a driver error with
        /// the migration identity and the exact SQL statement that failed.
        ///
        /// @param operation Whether the failure happened during apply or revert.
        /// @param timestamp The migration that failed.
        /// @param title Human-readable migration title.
        /// @param stepIndex Zero-based step index inside the migration plan.
        /// @param failedSql The SQL statement that produced the driver error.
        /// @param driverError The ODBC-level error info as received from the driver.
        MigrationException(Operation operation,
                           MigrationTimestamp timestamp,
                           std::string title,
                           std::size_t stepIndex,
                           std::string failedSql,
                           SqlErrorInfo driverError);

        /// Whether the failure occurred while applying or reverting.
        [[nodiscard]] Operation GetOperation() const noexcept
        {
            return _operation;
        }
        /// Timestamp of the failing migration.
        [[nodiscard]] MigrationTimestamp GetMigrationTimestamp() const noexcept
        {
            return _timestamp;
        }
        /// Human-readable title of the failing migration.
        [[nodiscard]] std::string const& GetMigrationTitle() const noexcept
        {
            return _title;
        }
        /// Zero-based step index inside the plan of the failing migration.
        [[nodiscard]] std::size_t GetStepIndex() const noexcept
        {
            return _stepIndex;
        }
        /// The exact SQL statement that the driver rejected.
        [[nodiscard]] std::string const& GetFailedSql() const noexcept
        {
            return _failedSql;
        }
        /// Raw driver error message, without the migration context prefix that
        /// `what()` and `info().message` decorate it with.
        [[nodiscard]] std::string const& GetDriverMessage() const noexcept
        {
            return _driverMessage;
        }

      private:
        Operation _operation;
        MigrationTimestamp _timestamp;
        std::string _title;
        std::size_t _stepIndex;
        std::string _failedSql;
        std::string _driverMessage;
    };

    /// Result of verifying a migration's checksum.
    ///
    /// @ingroup SqlMigration
    struct ChecksumVerificationResult
    {
        MigrationTimestamp timestamp; ///< The timestamp of the verified migration.
        std::string_view title;       ///< The title of the verified migration.
        std::string storedChecksum;   ///< The checksum stored in the database.
        std::string computedChecksum; ///< The checksum computed from the current migration definition.
        bool matches;                 ///< Whether the stored and computed checksums match.
    };

    /// Result of reverting multiple migrations.
    ///
    /// @ingroup SqlMigration
    struct RevertResult
    {
        std::vector<MigrationTimestamp> revertedTimestamps; ///< Successfully reverted migrations
        std::optional<MigrationTimestamp> failedAt;         ///< Migration that failed, if any
        std::string errorMessage;                           ///< Short error message if failed (driver message only)

        /// Title of the migration that failed (if any). Empty when no failure or
        /// when the failure happened before the migration could be located.
        std::string failedTitle;

        /// Zero-based step index inside the failed migration's plan. Meaningful
        /// only when `failedAt` is set and the failure came from a driver error
        /// (not from e.g. a missing registered migration).
        std::size_t failedStepIndex {};

        /// The exact SQL statement that failed, if available. Empty when the
        /// failure happened outside of SQL execution (e.g. missing Down()
        /// implementation, unregistered migration).
        std::string failedSql;

        /// SQLSTATE diagnostic code from the driver, if available.
        std::string sqlState;

        /// Native driver error code, if available.
        SQLINTEGER nativeErrorCode {};
    };

    /// Status summary of migrations.
    ///
    /// @ingroup SqlMigration
    struct MigrationStatus
    {
        size_t appliedCount {};        ///< Number of migrations that have been applied
        size_t pendingCount {};        ///< Number of migrations waiting to be applied
        size_t mismatchCount {};       ///< Number of applied migrations with checksum mismatches
        size_t unknownAppliedCount {}; ///< Number of applied migrations not found in registered list
        size_t totalRegistered {};     ///< Total number of registered migrations
    };

    /// Associates a software release with the highest migration timestamp present at release time.
    ///
    /// Releases are declared in source (typically a migration plugin) via
    /// `LIGHTWEIGHT_SQL_RELEASE(version, highestTimestamp)`. They let tools answer questions like
    /// "which migrations belong to release 6.7.0?" or "roll back everything applied after 6.7.0".
    ///
    /// The `highestTimestamp` is an inclusive upper bound: a migration `M` belongs to the release
    /// iff `prev_release_ts < M.timestamp <= highestTimestamp`, where `prev_release_ts` is the
    /// previous release's timestamp (or 0 if there is none).
    ///
    /// @ingroup SqlMigration
    struct MigrationRelease
    {
        /// Human-readable version string, e.g. "6.7.0".
        std::string version;
        /// Highest migration timestamp contained in this release (inclusive).
        MigrationTimestamp highestTimestamp;
    };

    /// Main API to use for managing SQL migrations
    ///
    /// This class is a singleton and can be accessed using the GetInstance() method.
    ///
    /// @ingroup SqlMigration
    class MigrationManager
    {
      public:
        /// Type alias for a list of migration pointers.
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

        /// Callback type invoked during migration execution to report progress.
        using ExecuteCallback =
            std::function<void(MigrationBase const& /*migration*/, size_t /*current*/, size_t /*total*/)>;

        /// Apply a single migration from a migration object.
        ///
        /// @param migration Pointer to the migration to apply.
        LIGHTWEIGHT_API void ApplySingleMigration(MigrationBase const& migration);

        /// @brief Variant of `ApplySingleMigration` that threads a caller-owned render
        /// context through to `ToSql`. Use this from loops over multiple migrations so
        /// the column-width cache (and other per-run compat state) accumulates across
        /// the sequence.
        LIGHTWEIGHT_API void ApplySingleMigration(MigrationBase const& migration, MigrationRenderContext& context);

        /// Revert a single migration from a migration object.
        ///
        /// @param migration Pointer to the migration to revert.
        LIGHTWEIGHT_API void RevertSingleMigration(MigrationBase const& migration);

        /// Apply all migrations that have not been applied yet.
        ///
        /// @param feedbackCallback Callback to be called for each migration.
        /// @return Number of applied migrations.
        LIGHTWEIGHT_API size_t ApplyPendingMigrations(ExecuteCallback const& feedbackCallback = {});

        /// Apply pending migrations whose timestamp is <= `targetInclusive`.
        ///
        /// Honors dependency-respecting topological order, just like
        /// `ApplyPendingMigrations`, and threads a single render context across
        /// the run so column-width state from earlier CREATE TABLEs is visible
        /// to later compat-aware INSERT/UPDATE rendering.
        ///
        /// If a pending migration with `ts <= target` declares a dependency on
        /// a migration whose timestamp is `> target` (i.e. excluded by the
        /// bound) and is not already applied, this throws — partial states
        /// that violate dependencies are refused up front.
        ///
        /// @param targetInclusive Highest timestamp to apply (inclusive).
        /// @param feedbackCallback Callback to be called for each migration.
        /// @return Number of applied migrations (may be zero if already past `targetInclusive`).
        /// @throws std::runtime_error if a dependency would cross the boundary.
        LIGHTWEIGHT_API size_t ApplyPendingMigrationsUpTo(MigrationTimestamp targetInclusive,
                                                          ExecuteCallback const& feedbackCallback = {});

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

        /// @brief Per-migration compat policy.
        ///
        /// Given a migration about to be applied (or previewed), returns the set of compat
        /// flags that should be active while rendering it. Plugins that ship legacy
        /// migrations use this to scope compat behaviour to their own timestamp range
        /// (e.g. `LupMigrationsPlugin` enables `lup-truncate` for migrations predating
        /// release 6.0.0). Unset means "strict for every migration".
        using CompatPolicy = std::function<std::set<std::string>(MigrationBase const&)>;

        /// @brief Installs a per-migration compat policy. Pass `{}` to clear.
        ///
        /// Typically called once from a plugin's static initializer. Replaces any policy
        /// previously installed — composition is the caller's responsibility for now.
        LIGHTWEIGHT_API void SetCompatPolicy(CompatPolicy policy);

        /// @brief Returns a view of the installed policy so it can be propagated across
        /// managers (plugin → central). Empty callable if no policy was installed.
        [[nodiscard]] LIGHTWEIGHT_API CompatPolicy const& GetCompatPolicy() const noexcept;

        /// @brief Composes an additional policy with the currently installed one. If no
        /// policy is installed, the argument becomes the policy as-is; otherwise both
        /// policies are consulted and their flag sets unioned per migration.
        LIGHTWEIGHT_API void ComposeCompatPolicy(CompatPolicy policy);

        /// @brief Returns the compat flags the current policy assigns to `migration`, or
        /// an empty set if no policy is installed.
        [[nodiscard]] LIGHTWEIGHT_API std::set<std::string> CompatFlagsFor(MigrationBase const& migration) const;

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

        /// @brief Variant of `PreviewMigration` that threads a caller-owned render
        /// context so the column-width cache accumulates across a sequence of preview
        /// calls. Used by `PreviewPendingMigrations` to render compat-aware dry runs.
        [[nodiscard]] LIGHTWEIGHT_API std::vector<std::string> PreviewMigrationWithContext(
            MigrationBase const& migration, MigrationRenderContext& context) const;

        /// @brief One row in the `RewriteChecksumsResult.entries` list.
        struct ChecksumRewriteEntry
        {
            MigrationTimestamp timestamp; ///< The migration whose stored checksum was rewritten.
            std::string_view title;       ///< Title of the registered migration (or "(Unknown Migration)").
            std::string oldChecksum;      ///< Stored checksum before the rewrite. Empty if there was none.
            std::string newChecksum;      ///< Stored checksum after the rewrite.
        };

        /// @brief Result of a `RewriteChecksums` call.
        struct RewriteChecksumsResult
        {
            std::vector<ChecksumRewriteEntry> entries;       ///< One entry per rewritten or would-be-rewritten row.
            std::vector<MigrationTimestamp> unregisteredTimestamps; ///< Applied rows whose migration is no longer registered.
            bool wasDryRun = false; ///< True if the call ran in dry-run mode.
        };

        /// @brief Snapshot of the schema the registered migrations *intend* to produce.
        ///
        /// Pure plan-walk — never executes SQL, never opens a connection. Folds the
        /// effects of every registered migration (up to an optional cut-off timestamp)
        /// into a per-table view of "the final shape" plus a chronological list of
        /// data steps and surviving indexes/releases. Used by:
        ///
        ///   - `dbtool fold` to emit a self-contained baseline (`.cpp` plugin or `.sql`)
        ///   - `HardReset` to know which tables the migrations would have created
        ///   - `UnicodeUpgradeTables` to know which char/varchar columns the migrations
        ///     now declare wide
        ///
        /// All three are pure operations — `FoldRegisteredMigrations` itself never
        /// executes a single statement. See `FoldRegisteredMigrations` for the API.
        struct PlanFoldingResult
        {
            /// Per-table state: ordered column declarations + per-table FK list.
            struct TableState
            {
                std::vector<SqlColumnDeclaration> columns;
                std::vector<SqlCompositeForeignKeyConstraint> compositeForeignKeys;
                bool ifNotExists = false;
            };

            /// (schema, table) → folded `TableState`. Insertion order is *not* preserved by
            /// `std::map` — for emission order use `creationOrder` below.
            std::map<SqlSchema::FullyQualifiedTableName, TableState> tables;

            /// Tables in *creation* order (first-time-seen). Reverse for safe DROP ordering
            /// when tearing the schema down.
            std::vector<SqlSchema::FullyQualifiedTableName> creationOrder;

            /// Indexes that survive folding (created on tables still present at end).
            std::vector<SqlCreateIndexPlan> indexes;

            /// One data step (INSERT/UPDATE/DELETE/RawSql) tagged with its source migration.
            struct DataStep
            {
                MigrationTimestamp sourceTimestamp;
                std::string sourceTitle;
                SqlMigrationPlanElement element;
            };

            /// Data steps in chronological order. **No coalescing** — the fold replays
            /// every data step verbatim, exactly as if migrations were applied in order.
            std::vector<DataStep> dataSteps;

            /// Releases declared via `LIGHTWEIGHT_SQL_RELEASE` that fall within the fold range.
            std::vector<MigrationRelease> releases;

            /// Migrations that contributed to the fold (timestamp + title). Used by emitters
            /// to write a header comment explaining what was collapsed.
            std::vector<std::pair<MigrationTimestamp, std::string>> foldedMigrations;
        };

        /// @brief Pure plan-walk that folds the effect of every registered migration.
        ///
        /// Visits each migration's `Up()` plan in timestamp order (or up to
        /// `upToInclusive` if provided) and accumulates the cumulative end-state into a
        /// `PlanFoldingResult`. **Never** executes SQL or touches a database connection
        /// — the supplied formatter is only used to build the in-memory plan elements.
        ///
        /// @param formatter Formatter used by the migration query builder while walking
        ///                  each migration's `Up()`. Any standard formatter works; the
        ///                  walk inspects plan element shapes, not rendered SQL.
        /// @param upToInclusive If set, fold only migrations with `timestamp <= upToInclusive`.
        ///                     If unset, fold all registered migrations.
        ///
        /// @return The folded snapshot. Safe to call without a `MigrationManager`-attached
        ///         data mapper.
        [[nodiscard]] LIGHTWEIGHT_API PlanFoldingResult FoldRegisteredMigrations(
            SqlQueryFormatter const& formatter,
            std::optional<MigrationTimestamp> upToInclusive = std::nullopt) const;

        /// @brief Result of a `HardReset` call.
        struct HardResetResult
        {
            bool wasDryRun = false;
            /// Tables the registered migrations *would* have created and were also present
            /// in the live DB — these were dropped (or would be dropped on a real run).
            std::vector<SqlSchema::FullyQualifiedTableName> droppedTables;
            /// Tables registered migrations declare but that aren't in the live DB —
            /// nothing to do for these. Reported for visibility only.
            std::vector<SqlSchema::FullyQualifiedTableName> absentTables;
            /// Tables in the live DB the registered migrations don't know about — left
            /// alone (user-owned). Reported prominently so operators notice.
            std::vector<SqlSchema::FullyQualifiedTableName> preservedTables;
            /// Whether the `schema_migrations` table itself was dropped (always true on a
            /// real run, false on dry-run).
            bool schemaMigrationsDropped = false;
        };

        /// @brief Drops every table the registered migrations would create (incl.
        /// `schema_migrations`), preserving any user-created tables.
        ///
        /// Folds all registered migrations to compute the migration-owned set, intersects
        /// with the live schema (via `SqlSchema::ReadAllTables`), then drops the matching
        /// live tables in **reverse** creation order with `cascade=true ifExists=true`. The
        /// `schema_migrations` table is dropped explicitly because it's created outside the
        /// registered-migrations stream.
        ///
        /// Tables present in the live DB but not in the migration plan are left alone and
        /// reported under `preservedTables` so operators can spot them.
        ///
        /// @param dryRun When true, returns the would-be plan without writing anything.
        [[nodiscard]] LIGHTWEIGHT_API HardResetResult HardReset(bool dryRun = false);

        /// @brief One column upgrade entry in `UnicodeUpgradeResult`.
        struct ColumnUpgradeEntry
        {
            SqlSchema::FullyQualifiedTableName table;
            std::string column;
            /// Live byte-counted type the column currently has.
            SqlColumnTypeDefinition liveType;
            /// Char-counted type the migrations now declare for this column.
            SqlColumnTypeDefinition intendedType;
            bool nullable = true;
        };

        /// @brief Result of an `UnicodeUpgradeTables` call.
        struct UnicodeUpgradeResult
        {
            bool wasDryRun = false;
            /// Columns whose live type drifted from the intended type and were upgraded
            /// (or would be on a real run).
            std::vector<ColumnUpgradeEntry> columns;
            /// Foreign keys that had to be dropped + re-added to upgrade their
            /// participating columns. Reported so operators see the FK churn.
            std::vector<SqlCompositeForeignKeyConstraint> rebuiltForeignKeys;
        };

        /// @brief Rewrites legacy `VARCHAR/CHAR` columns to `NVARCHAR/NCHAR` where the
        /// registered migrations now declare wide types.
        ///
        /// Compares the folded plan's intended column types against `SqlSchema::ReadAllTables`
        /// output; an upgrade is triggered iff intended is `NVarchar`/`NChar` AND live is
        /// `Varchar`/`Char` with the same `size`. Foreign keys that touch any upgrade column
        /// are dropped before the alter and re-added afterwards. Cross-backend; SQLite
        /// uses the in-tree `RebuildSqliteTable` recipe under the hood.
        ///
        /// @param dryRun When true, returns the would-be diff without writing anything.
        [[nodiscard]] LIGHTWEIGHT_API UnicodeUpgradeResult UnicodeUpgradeTables(bool dryRun = false);

        /// @brief Re-stamps `schema_migrations.checksum` rows that have drifted.
        ///
        /// Applied migrations whose stored checksum no longer matches the current
        /// `MigrationBase::ComputeChecksum` output are updated in-place. Migrations that
        /// match are left alone. Rows that reference a migration which is no longer
        /// registered (e.g. removed from the codebase) are surfaced via
        /// `RewriteChecksumsResult.unregisteredTimestamps` and *not* touched.
        ///
        /// This is a recovery tool used when a regen of generated migrations changes
        /// their byte-level shape but not their logical effect — running it without
        /// understanding *why* the checksum drifted would defeat the integrity check.
        ///
        /// @param dryRun When true, returns the would-be diff without writing anything.
        [[nodiscard]] LIGHTWEIGHT_API RewriteChecksumsResult RewriteChecksums(bool dryRun = false);

        /// Preview SQL statements for all pending migrations without executing.
        ///
        /// This is useful for dry-run mode to see what SQL would be executed.
        ///
        /// @param feedbackCallback Optional callback to be called for each migration.
        /// @return Vector of all SQL statements that would be executed.
        [[nodiscard]] LIGHTWEIGHT_API std::vector<std::string> PreviewPendingMigrations(
            ExecuteCallback const& feedbackCallback = {}) const;

        /// Preview SQL for pending migrations whose timestamp is <= `targetInclusive`.
        ///
        /// Bounded counterpart of `PreviewPendingMigrations`. Same dependency
        /// rules as `ApplyPendingMigrationsUpTo`: if any included migration
        /// depends on an excluded pending migration, this throws.
        ///
        /// @param targetInclusive Highest timestamp to preview (inclusive).
        /// @param feedbackCallback Optional callback to be called for each migration.
        /// @return Vector of all SQL statements that would be executed.
        /// @throws std::runtime_error if a dependency would cross the boundary.
        [[nodiscard]] LIGHTWEIGHT_API std::vector<std::string> PreviewPendingMigrationsUpTo(
            MigrationTimestamp targetInclusive, ExecuteCallback const& feedbackCallback = {}) const;

        /// Verify checksums of all applied migrations.
        ///
        /// Compares the stored checksums in the database with the computed checksums
        /// of the current migration definitions. This helps detect if migrations
        /// have been modified after they were applied.
        ///
        /// @return Vector of verification results for migrations with mismatched or missing checksums.
        [[nodiscard]] LIGHTWEIGHT_API std::vector<ChecksumVerificationResult> VerifyChecksums() const;

        /// Mark a migration as applied without executing its Up() method.
        ///
        /// This is useful for:
        /// - Baseline migrations when setting up an existing database
        /// - Marking migrations that were applied manually or through other means
        /// - Skipping migrations that are not applicable to a specific environment
        ///
        /// @param migration The migration to mark as applied.
        /// @throws std::runtime_error if the migration is already applied.
        LIGHTWEIGHT_API void MarkMigrationAsApplied(MigrationBase const& migration);

        /// Revert all migrations applied after the target timestamp.
        ///
        /// This method reverts migrations in reverse chronological order,
        /// rolling back from the most recent to just after the target.
        /// The target migration itself is NOT reverted.
        ///
        /// @param target Target timestamp to revert to. Migrations > target are reverted.
        /// @param feedbackCallback Optional callback for progress updates.
        /// @return Result containing list of reverted timestamps or error information.
        /// @note Stops on first error. Previously reverted migrations in this call are NOT rolled back.
        [[nodiscard]] LIGHTWEIGHT_API RevertResult RevertToMigration(MigrationTimestamp target,
                                                                     ExecuteCallback const& feedbackCallback = {});

        /// Get a summary status of all migrations.
        ///
        /// This method provides a quick overview of the migration state without
        /// needing to iterate through individual migrations.
        ///
        /// @return Status struct with counts of applied, pending, and mismatched migrations.
        [[nodiscard]] LIGHTWEIGHT_API MigrationStatus GetMigrationStatus() const;

        /// Validate that all registered migrations have satisfiable dependencies.
        ///
        /// For each pending migration, verifies that every declared dependency is either
        /// already applied or itself pending (so it will be applied first due to topological
        /// ordering). Also detects cycles among pending migrations.
        ///
        /// @throws std::runtime_error if any dependency is unresolved or a cycle is found.
        LIGHTWEIGHT_API void ValidateDependencies() const;

        /// Register a release marker for a software version.
        ///
        /// Typically called from `LIGHTWEIGHT_SQL_RELEASE(version, timestamp)` at static init time.
        /// Releases are ordered by `highestTimestamp`; two releases may not share the same
        /// `highestTimestamp`, and the same version string may not be registered twice.
        ///
        /// @param version Human-readable version, e.g. "6.7.0".
        /// @param highestTimestamp Highest migration timestamp (inclusive) that belongs to this release.
        /// @throws std::runtime_error on duplicate version or duplicate timestamp.
        LIGHTWEIGHT_API void RegisterRelease(std::string version, MigrationTimestamp highestTimestamp);

        /// Remove all registered releases. Useful for resetting state in tests.
        LIGHTWEIGHT_API void RemoveAllReleases();

        /// Get all registered releases, sorted ascending by `highestTimestamp`.
        [[nodiscard]] LIGHTWEIGHT_API std::vector<MigrationRelease> const& GetAllReleases() const noexcept;

        /// Find a release by exact version string match.
        ///
        /// @return Pointer to the matching release, or nullptr if no release with that version is registered.
        [[nodiscard]] LIGHTWEIGHT_API MigrationRelease const* FindReleaseByVersion(std::string_view version) const noexcept;

        /// Find the release whose timestamp range contains `timestamp`.
        ///
        /// Returns the release with the smallest `highestTimestamp` that is `>= timestamp`.
        /// Returns nullptr if `timestamp` is greater than every registered release's highestTimestamp
        /// (i.e., the migration is post-all-releases / unreleased).
        [[nodiscard]] LIGHTWEIGHT_API MigrationRelease const* FindReleaseForTimestamp(
            MigrationTimestamp timestamp) const noexcept;

        /// Get all registered migrations belonging to a given release.
        ///
        /// A migration `M` belongs to release `R` iff
        /// `prev_release_ts < M.timestamp <= R.highestTimestamp`, where `prev_release_ts` is the
        /// previous release's timestamp (or 0 if `R` is the first release).
        ///
        /// @param version The version string to look up.
        /// @return Migrations in the release, ordered by timestamp. Empty if the version is unknown.
        [[nodiscard]] LIGHTWEIGHT_API MigrationList GetMigrationsForRelease(std::string_view version) const;

      private:
        /// Return the pending list in dependency-respecting order.
        ///
        /// Dependencies among pending migrations are resolved via topological sort.
        /// Migrations with no dependency between them keep timestamp order.
        /// Throws on missing deps or cycles.
        [[nodiscard]] MigrationList TopoSortPending(MigrationList pending,
                                                    std::vector<MigrationTimestamp> const& applied) const;

        /// @brief Builds a fresh, policy-agnostic render context. The per-migration
        /// compat knobs are layered on by `ApplySingleMigration` / `PreviewMigrationWithContext`
        /// before rendering a given migration's steps.
        [[nodiscard]] static MigrationRenderContext MakeRenderContext();

        MigrationList _migrations;
        std::vector<MigrationRelease> _releases;
        mutable DataMapper* _dataMapper { nullptr };
        CompatPolicy _compatPolicy;
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
        /// Default copy constructor.
        MigrationBase(MigrationBase const&) = default;
        MigrationBase(MigrationBase&&) = delete;
        /// Default copy assignment operator.
        MigrationBase& operator=(MigrationBase const&) = default;
        MigrationBase& operator=(MigrationBase&&) = delete;
        /// Constructs a migration with the given timestamp and title.
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
        virtual void Down(SqlMigrationQueryBuilder& plan) const
        {
            (void) plan;
        }

        /// Check if this migration has a Down() implementation for rollback.
        ///
        /// This method determines whether the migration can be safely reverted.
        /// The default implementation returns false. Derived classes that implement
        /// Down() should override this to return true.
        ///
        /// @return true if Down() is implemented and can revert this migration.
        [[nodiscard]] virtual bool HasDownImplementation() const noexcept
        {
            return false;
        }

        /// Get the timestamps of migrations that this migration depends on.
        ///
        /// Dependencies are enforced at apply time: each declared dependency must be
        /// registered and applied (or pending in the same run, in dependency order)
        /// before this migration will run. The default implementation returns no
        /// dependencies, preserving timestamp-ordered apply behavior.
        ///
        /// @return Vector of dependency timestamps. Empty if this migration has none.
        [[nodiscard]] virtual std::vector<MigrationTimestamp> GetDependencies() const
        {
            return {};
        }

        /// Get the author of the migration, if any.
        ///
        /// @return Author string, empty if not set.
        [[nodiscard]] virtual std::string_view GetAuthor() const noexcept
        {
            return {};
        }

        /// Get the long-form description of the migration, if any.
        ///
        /// This is a multi-line description in addition to the short title.
        ///
        /// @return Description string, empty if not set.
        [[nodiscard]] virtual std::string_view GetDescription() const noexcept
        {
            return {};
        }

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
    /// Optional metadata attached to a Migration at construction time.
    ///
    /// All fields are optional. Unset fields behave as if the feature
    /// were not used (e.g. empty dependencies preserves timestamp order).
    ///
    /// @ingroup SqlMigration
    struct MigrationMetadata
    {
        /// Migrations that must be applied before this one.
        std::vector<MigrationTimestamp> dependencies {};
        /// Author of the migration. Recorded in schema_migrations when the migration is applied.
        std::string_view author {};
        /// Long-form description. Recorded in schema_migrations when the migration is applied.
        std::string_view description {};
    };

    template <uint64_t TsValue>
    class Migration: public MigrationBase
    {
      public:
        /// The migration's timestamp, available at compile time from the type itself.
        ///
        /// Exposed so @ref TimestampOf can extract the timestamp without
        /// having to repeat the literal at every use site (e.g. in dependency lists).
        static constexpr MigrationTimestamp TimeStamp { TsValue };

        /// Create a new migration.
        ///
        /// @param title Title of the migration.
        /// @param up Function to apply the migration.
        /// @param down Function to revert the migration (optional).
        Migration(std::string_view title,
                  std::function<void(SqlMigrationQueryBuilder&)> const& up,
                  std::function<void(SqlMigrationQueryBuilder&)> const& down = {}):
            MigrationBase(TimeStamp, title),
            _up { up },
            _down { down }
        {
        }

        /// Create a new migration with optional metadata (dependencies, author, description).
        ///
        /// @param title Title of the migration.
        /// @param metadata Optional metadata including dependencies, author, and description.
        /// @param up Function to apply the migration.
        /// @param down Function to revert the migration (optional).
        Migration(std::string_view title,
                  MigrationMetadata metadata,
                  std::function<void(SqlMigrationQueryBuilder&)> const& up,
                  std::function<void(SqlMigrationQueryBuilder&)> const& down = {}):
            MigrationBase(TimeStamp, title),
            _up { up },
            _down { down },
            _metadata { std::move(metadata) }
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

        /// Check if this migration has a Down() implementation.
        ///
        /// @return true if a down function was provided.
        [[nodiscard]] bool HasDownImplementation() const noexcept override
        {
            return static_cast<bool>(_down);
        }

        /// Get the declared dependencies for this migration.
        [[nodiscard]] std::vector<MigrationTimestamp> GetDependencies() const override
        {
            return _metadata.dependencies;
        }

        /// Get the author of this migration.
        [[nodiscard]] std::string_view GetAuthor() const noexcept override
        {
            return _metadata.author;
        }

        /// Get the long-form description of this migration.
        [[nodiscard]] std::string_view GetDescription() const noexcept override
        {
            return _metadata.description;
        }

      private:
        std::function<void(SqlMigrationQueryBuilder&)> _up;
        std::function<void(SqlMigrationQueryBuilder&)> _down;
        MigrationMetadata _metadata {};
    };

    /// Extracts the @ref MigrationTimestamp from a migration type that exposes a static
    /// constexpr `TimeStamp` member.
    ///
    /// Works with both the class template @ref Migration and the struct generated by the
    /// @ref LIGHTWEIGHT_SQL_MIGRATION macro. Use via `decltype`:
    ///
    /// @code
    /// auto migrationA = SqlMigration::Migration<202601010001>("dep base", [](auto&){ ... });
    /// auto tsA = TimestampOf<decltype(migrationA)>; // MigrationTimestamp { 202601010001 }
    /// @endcode
    ///
    /// @tparam T A migration type exposing `static constexpr MigrationTimestamp TimeStamp`.
    template <typename T>
    inline constexpr MigrationTimestamp TimestampOf = T::TimeStamp;

    namespace detail
    {
        /// RAII registrar used by `LIGHTWEIGHT_SQL_RELEASE` to register a release with the
        /// migration manager at static-initialization time. Not intended for direct use.
        ///
        /// @ingroup SqlMigration
        struct ReleaseRegistrar
        {
            /// Registers `{version, highestTimestamp}` with the singleton manager.
            ReleaseRegistrar(std::string version, MigrationTimestamp highestTimestamp)
            {
                MigrationManager::GetInstance().RegisterRelease(std::move(version), highestTimestamp);
            }
        };
    } // namespace detail

} // namespace SqlMigration

} // namespace Lightweight

#define _LIGHTWEIGHT_CONCATENATE(s1, s2)       s1##s2
#define _LIGHTWEIGHT_CONCATENATE_INNER(s1, s2) _LIGHTWEIGHT_CONCATENATE(s1, s2)

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
#define LIGHTWEIGHT_SQL_MIGRATION(timestamp, description)                                                              \
    struct Migration_##timestamp: public Lightweight::SqlMigration::MigrationBase                                      \
    {                                                                                                                  \
        /** The migration's timestamp, exposed so @ref TimestampOf can extract it from the type. */                    \
        static constexpr Lightweight::SqlMigration::MigrationTimestamp TimeStamp { static_cast<uint64_t>(timestamp) }; \
        explicit Migration_##timestamp():                                                                              \
            Lightweight::SqlMigration::MigrationBase(TimeStamp, description)                                           \
        {                                                                                                              \
        }                                                                                                              \
                                                                                                                       \
        void Up(Lightweight::SqlMigrationQueryBuilder& plan) const override;                                           \
        void Down(Lightweight::SqlMigrationQueryBuilder& /*plan*/) const override {}                                   \
    };                                                                                                                 \
                                                                                                                       \
    static Migration_##timestamp _LIGHTWEIGHT_CONCATENATE(migration_, timestamp);                                      \
                                                                                                                       \
    void Migration_##timestamp::Up(Lightweight::SqlMigrationQueryBuilder& plan) const

/// @brief Associates a software release (version string) with the highest migration timestamp
/// present at the time of that release.
///
/// Declare one `LIGHTWEIGHT_SQL_RELEASE` per cut release, alongside the migrations that belong to it.
/// The macro registers with the migration manager at static-initialization time. Multiple releases
/// may coexist in the same translation unit.
///
/// @param version A string literal, e.g. `"6.7.0"`.
/// @param highestTimestamp An unsigned integer literal matching the timestamp format used by migrations.
///
/// @code
/// LIGHTWEIGHT_SQL_MIGRATION(20260101120000, "Initial schema") { ... }
/// LIGHTWEIGHT_SQL_RELEASE("6.6.0", 20260101120000);
///
/// LIGHTWEIGHT_SQL_MIGRATION(20260501120000, "Add orders table") { ... }
/// LIGHTWEIGHT_SQL_RELEASE("6.7.0", 20260501120000);
/// @endcode
///
/// @ingroup SqlMigration
#define LIGHTWEIGHT_SQL_RELEASE(version, highestTimestamp)                                                                 \
    static ::Lightweight::SqlMigration::detail::ReleaseRegistrar _LIGHTWEIGHT_CONCATENATE_INNER(_lw_release_, __COUNTER__) \
    {                                                                                                                      \
        (version), ::Lightweight::SqlMigration::MigrationTimestamp                                                         \
        {                                                                                                                  \
            (highestTimestamp)                                                                                             \
        }                                                                                                                  \
    }
