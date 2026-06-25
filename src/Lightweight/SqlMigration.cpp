// SPDX-License-Identifier: Apache-2.0

#include "DataBinder/SqlVariant.hpp"
#include "DataMapper/DataMapper.hpp"
#include "QueryFormatter/SQLiteFormatter.hpp"
#include "SqlAdvisoryLock.hpp"
#include "SqlBackup/Sha256.hpp"
#include "SqlConnection.hpp"
#include "SqlErrorDetection.hpp"
#include "SqlLogger.hpp"
#include "SqlMigration.hpp"
#include "SqlSchema.hpp"
#include "SqlTransaction.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <limits>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <version>

namespace Lightweight::SqlMigration
{

namespace
{
    /// Compose a single-line summary suitable for `what()`. The detailed
    /// breakdown (title/step/sql/driver msg) lives in the structured fields;
    /// this keeps the base `std::runtime_error::what()` short enough for
    /// single-line terminal output while still naming the failing migration.
    std::string FormatMigrationWhat(MigrationException::Operation op,
                                    MigrationTimestamp timestamp,
                                    std::string_view title,
                                    std::size_t stepIndex,
                                    SqlErrorInfo const& driverError)
    {
        auto const* const verb = op == MigrationException::Operation::Apply ? "apply" : "rollback";
        return std::format(
            "Failed to {} migration {} '{}' at step {}: {}", verb, timestamp.value, title, stepIndex, driverError.message);
    }

    /// Convert a (possibly empty) author string into the storage form used by
    /// `SchemaMigration::author`. Empty input maps to `std::nullopt` so the row
    /// is rendered with a SQL NULL rather than an empty `VARCHAR(128)`.
    std::optional<SqlString<128>> MakeOptionalSqlString128(std::string_view value)
    {
        if (value.empty())
            return std::nullopt;
        return SqlString<128> { value };
    }

    /// Same as `MakeOptionalSqlString128`, sized for `SchemaMigration::description`.
    std::optional<SqlString<1024>> MakeOptionalSqlString1024(std::string_view value)
    {
        if (value.empty())
            return std::nullopt;
        return SqlString<1024> { value };
    }
} // namespace

MigrationException::MigrationException(Operation operation,
                                       MigrationTimestamp timestamp,
                                       std::string title,
                                       std::size_t stepIndex,
                                       std::string failedSql,
                                       SqlErrorInfo driverError):
    // Reconstruct the SqlException base with a message that names the migration
    // — so every existing `catch (SqlException const&)` path still sees useful
    // context in `info().message` and `what()` without having to know about
    // the new type. Keep the native driver fields intact.
    SqlException(SqlErrorInfo { .nativeErrorCode = driverError.nativeErrorCode,
                                .sqlState = driverError.sqlState,
                                .message = FormatMigrationWhat(operation, timestamp, title, stepIndex, driverError) }),
    _operation { operation },
    _timestamp { timestamp },
    _title { std::move(title) },
    _stepIndex { stepIndex },
    _failedSql { std::move(failedSql) },
    _driverMessage { std::move(driverError.message) }
{
}

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
    Field<std::optional<SqlString<65>>> checksum;      // SHA-256 hex (64 chars + null, optional for backward compatibility)
    Field<std::optional<SqlDateTime>> applied_at;      // Timestamp when migration was applied
    Field<std::optional<SqlString<128>>> author;       // Optional author recorded from MigrationBase::GetAuthor()
    Field<std::optional<SqlString<1024>>> description; // Optional long-form description
    Field<std::optional<uint64_t>>
        execution_duration_ms; // Wall-clock duration of Up() in milliseconds (null for MarkAsApplied)

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

void MigrationManager::SetCompatPolicy(CompatPolicy policy)
{
    _compatPolicy = std::move(policy);
}

MigrationManager::CompatPolicy const& MigrationManager::GetCompatPolicy() const noexcept
{
    return _compatPolicy;
}

void MigrationManager::ComposeCompatPolicy(CompatPolicy policy)
{
    if (!policy)
        return;
    if (!_compatPolicy)
    {
        _compatPolicy = std::move(policy);
        return;
    }
    // Compose: both policies are consulted and their flag sets unioned per migration.
    _compatPolicy = [lhs = std::move(_compatPolicy), rhs = std::move(policy)](MigrationBase const& m) {
        auto flags = lhs(m);
        auto extra = rhs(m);
        flags.insert(extra.begin(), extra.end());
        return flags;
    };
}

std::set<std::string> MigrationManager::CompatFlagsFor(MigrationBase const& migration) const
{
    if (!_compatPolicy)
        return {};
    return _compatPolicy(migration);
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

namespace
{
    /// Rejects schema names that would let SQL fragments escape the
    /// `SET search_path TO "<schema>", public` template. We're not trying to
    /// be liberal: the schema name is supplied by an operator via `--schema`
    /// or a profile/GUI input, and a strict ASCII identifier whitelist
    /// covers `dbo`, `lasa`, `public`, etc. without requiring quote-doubling.
    bool IsSafeSchemaIdentifier(std::string_view schema) noexcept
    {
        if (schema.empty())
            return false;
        return std::ranges::all_of(schema, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        });
    }
} // namespace

void MigrationManager::SetDefaultSchema(std::string schema)
{
    if (schema == _defaultSchema)
        return;

    if (!schema.empty() && !IsSafeSchemaIdentifier(schema))
        throw std::invalid_argument(std::format(
            "MigrationManager::SetDefaultSchema: rejecting schema name '{}' — only [A-Za-z0-9_] is permitted.", schema));

    _defaultSchema = std::move(schema);

    // Reset and (re-)install the post-connect hook. Always reset first so a
    // previously-installed schema hook from this manager doesn't leak when
    // the user clears the schema by passing an empty string. We don't try to
    // compose with hooks installed by other callers — at the time this
    // method is invoked (dbtool/GUI startup, before any connection has been
    // opened) there are typically none, and the test helper hooks in
    // `Utils.hpp` install themselves *per test* so they'll override us
    // freely.
    SqlConnection::ResetPostConnectedHook();

    if (_defaultSchema.empty())
        return;

    auto schemaCopy = _defaultSchema;
    SqlConnection::SetPostConnectedHook([schemaCopy = std::move(schemaCopy)](SqlConnection& conn) {
        auto const* formatter = SqlQueryFormatter::Get(conn.ServerType());
        if (formatter == nullptr)
            return;
        auto const statement = formatter->SetDefaultSchemaStatement(schemaCopy);
        if (statement.empty())
            return; // DBMS has no session-level default-schema concept.
        SqlStatement stmt(conn);
        (void) stmt.ExecuteDirect(statement);
    });
}

std::string_view MigrationManager::DefaultSchema() const noexcept
{
    return _defaultSchema;
}

std::vector<MigrationTimestamp> MigrationManager::GetAppliedMigrationIds() const
{
    auto result = std::vector<MigrationTimestamp> {};

    // Database half: read `schema_migrations` rows when the table exists. When
    // it does not (fresh DB, or any read-only command issued before the first
    // apply), `Query` throws and we silently fall through to the overlay-only
    // path. We deliberately *do not* create the table here — read paths must
    // remain side-effect free so `dbtool status` against an untouched legacy
    // database neither writes nor requires write permissions.
    try
    {
        auto const records = GetDataMapper().Query<SchemaMigration>().OrderBy("version", SqlResultOrdering::ASCENDING).All();
        result.reserve(records.size());
        for (auto const& record: records)
            result.emplace_back(MigrationTimestamp { record.version.Value() });
    }
    catch (SqlException const& ex)
    {
        // Table absent or otherwise unreadable — fall back to overlay-only.
        Log(std::format("schema_migrations read fell through to overlay-only: {}", ex.what()));
    }

    // Overlay half: merge any virtually-applied IDs (populated by plugin
    // post-init hooks) that the caller wants to be observable as applied even
    // before `schema_migrations` has been materialised. Both inputs are sorted
    // ascending; `set_union` produces a deduplicated, sorted result in one
    // pass.
    if (!_virtualAppliedIds.empty())
    {
        auto merged = std::vector<MigrationTimestamp> {};
        merged.reserve(result.size() + _virtualAppliedIds.size());
        std::ranges::set_union(result, _virtualAppliedIds, std::back_inserter(merged));
        result = std::move(merged);
    }

    return result;
}

void MigrationManager::AddVirtualAppliedMigrations(std::span<MigrationTimestamp const> timestamps)
{
    if (timestamps.empty())
        return;

    // Merge into the existing overlay, preserving the sorted+deduped invariant
    // so `GetAppliedMigrationIds`'s `set_union` stays correct on subsequent
    // calls. Sort the incoming chunk in-place via a local copy so the caller's
    // span is not mutated.
    auto incoming = std::vector<MigrationTimestamp> { timestamps.begin(), timestamps.end() };
    std::ranges::sort(incoming);
    auto const [removeBegin, removeEnd] = std::ranges::unique(incoming);
    incoming.erase(removeBegin, removeEnd);

    auto merged = std::vector<MigrationTimestamp> {};
    merged.reserve(_virtualAppliedIds.size() + incoming.size());
    std::ranges::set_union(_virtualAppliedIds, incoming, std::back_inserter(merged));
    _virtualAppliedIds = std::move(merged);
}

void MigrationManager::ClearVirtualAppliedMigrations()
{
    _virtualAppliedIds.clear();
}

void MigrationManager::SetLogSink(LogSink sink)
{
    _logSink = std::move(sink);
}

void MigrationManager::Log(std::string_view message) const
{
    if (_logSink)
        _logSink(message);
}

void MigrationManager::PersistVirtualAppliedMigrations()
{
    // We are crossing into write territory — materialise the history table
    // exactly here, so read-only commands never trigger it. Done
    // unconditionally (not gated on a non-empty overlay) because every write
    // path that calls this also goes on to `dm.CreateExplicit(SchemaMigration{...})`
    // for its own new row, which would fail with "no such table" on a fresh
    // DB without an overlay otherwise. `CreateMigrationHistory` is idempotent
    // and swallows "already exists" errors, so the cost on subsequent writes
    // is a single catch on the SQL driver's table-exists check.
    CreateMigrationHistory();

    if (_virtualAppliedIds.empty())
        return;

    // Move the overlay onto the stack before any writes so re-entrant calls
    // (e.g. an inner `MarkMigrationAsApplied` triggered by a future plugin
    // hook) cannot observe a partially-drained state.
    auto pending = std::move(_virtualAppliedIds);
    _virtualAppliedIds.clear();

    auto& dm = GetDataMapper();

    // Re-read the rows that are *physically* present so the overlay is filtered
    // against actual database state. This matters when a plugin re-installed
    // an overlay entry that the database already knows about (e.g. running
    // `dbtool migrate` twice on the same legacy DB without restarting).
    auto realApplied = std::vector<MigrationTimestamp> {};
    try
    {
        auto const records = dm.Query<SchemaMigration>().OrderBy("version", SqlResultOrdering::ASCENDING).All();
        realApplied.reserve(records.size());
        for (auto const& record: records)
            realApplied.emplace_back(MigrationTimestamp { record.version.Value() });
    }
    catch (SqlException const& ex)
    {
        // `CreateMigrationHistory` just ran without throwing, so the table
        // should exist. If `Query` still failed it is a real driver error and
        // not the missing-table case — let it surface from the writes below.
        Log(std::format("schema_migrations re-read after CreateMigrationHistory failed: {}", ex.what()));
    }

    // Index `_migrations` by timestamp so we can resolve each overlay entry
    // back to a registered migration without an O(N*M) scan.
    auto byTimestamp = std::unordered_map<uint64_t, MigrationBase const*> {};
    byTimestamp.reserve(_migrations.size());
    for (auto const* migration: _migrations)
        byTimestamp.emplace(migration->GetTimestamp().value, migration);

    for (auto const& timestamp: pending)
    {
        if (std::ranges::contains(realApplied, timestamp))
            continue;

        auto const it = byTimestamp.find(timestamp.value);
        if (it == byTimestamp.end())
        {
            // Overlay entry points at a timestamp that no longer matches any
            // registered migration (plugin was unloaded, or a plugin re-issued
            // an old overlay against a manager that has since dropped the
            // migration). Skipping is the right call: there is no checksum or
            // metadata to record, and re-throwing would make the first
            // `migrate` invocation of a stale overlay un-recoverable.
            continue;
        }
        auto const& migration = *it->second;

        auto const checksum = migration.ComputeChecksum(dm.Connection().QueryFormatter());
        dm.CreateExplicit(SchemaMigration {
            .version = migration.GetTimestamp().value,
            .checksum = checksum,
            .applied_at = SqlDateTime::Now(),
            .author = MakeOptionalSqlString128(migration.GetAuthor()),
            .description = MakeOptionalSqlString1024(migration.GetDescription()),
            .execution_duration_ms = std::nullopt,
        });
    }
}

MigrationManager::MigrationList MigrationManager::GetPending() const noexcept
{
    auto const applied = GetAppliedMigrationIds();
    auto pending = MigrationList {};
    for (auto const* migration: _migrations)
        if (!std::ranges::contains(applied, migration->GetTimestamp()))
            pending.push_back(migration);

    try
    {
        return TopoSortPending(std::move(pending), applied);
    }
    catch (...)
    {
        // noexcept contract: fall back to timestamp-ordered list on error.
        // The error will surface again when ApplyPendingMigrations/ValidateDependencies is called.
        auto fallback = MigrationList {};
        for (auto const* migration: _migrations)
            if (!std::ranges::contains(applied, migration->GetTimestamp()))
                fallback.push_back(migration);
        return fallback;
    }
}

namespace
{
    /// Dependency graph over the pending migration subset.
    ///
    /// Only edges whose source is also pending are represented. Dependencies already
    /// in the applied set are considered satisfied and are not modeled.
    struct PendingDependencyGraph
    {
        std::unordered_map<uint64_t, size_t> inDegree;
        std::unordered_map<uint64_t, std::vector<uint64_t>> dependents;
        std::unordered_map<uint64_t, MigrationBase const*> pendingByTs;
    };

    [[nodiscard]] bool AnyPendingHasDependencies(MigrationManager::MigrationList const& pending)
    {
        return std::ranges::any_of(pending, [](MigrationBase const* m) { return !m->GetDependencies().empty(); });
    }

    [[nodiscard]] std::unordered_set<uint64_t> ToTimestampSet(std::vector<MigrationTimestamp> const& ids)
    {
        std::unordered_set<uint64_t> result;
        result.reserve(ids.size());
        for (auto const& ts: ids)
            result.insert(ts.value);
        return result;
    }

    [[nodiscard]] std::unordered_set<uint64_t> ToTimestampSet(MigrationManager::MigrationList const& migrations)
    {
        std::unordered_set<uint64_t> result;
        result.reserve(migrations.size());
        for (auto const* migration: migrations)
            result.insert(migration->GetTimestamp().value);
        return result;
    }

    /// Build the dependency graph over the pending subset. Throws if a declared
    /// dependency references an unknown (never-registered) migration.
    [[nodiscard]] PendingDependencyGraph BuildPendingGraph(MigrationManager::MigrationList const& pending,
                                                           std::unordered_set<uint64_t> const& appliedSet,
                                                           std::unordered_set<uint64_t> const& registeredSet)
    {
        PendingDependencyGraph graph;

        for (auto const* migration: pending)
        {
            auto const ts = migration->GetTimestamp().value;
            graph.inDegree[ts] = 0;
            graph.pendingByTs[ts] = migration;
        }

        for (auto const* migration: pending)
        {
            auto const ts = migration->GetTimestamp().value;
            for (auto const& dep: migration->GetDependencies())
            {
                if (appliedSet.contains(dep.value))
                    continue;
                if (!registeredSet.contains(dep.value))
                {
                    throw std::runtime_error(
                        std::format("Migration '{}' (timestamp {}) depends on unknown migration with timestamp {}.",
                                    migration->GetTitle(),
                                    ts,
                                    dep.value));
                }
                graph.dependents[dep.value].push_back(ts);
                ++graph.inDegree[ts];
            }
        }
        return graph;
    }

    [[nodiscard]] std::string FormatCycleError(std::unordered_map<uint64_t, size_t> const& inDegree)
    {
        auto cycleNodes = std::vector<uint64_t> {};
        for (auto const& [ts, deg]: inDegree)
            if (deg != 0)
                cycleNodes.push_back(ts);
        std::ranges::sort(cycleNodes);

        std::string joined;
        for (auto const ts: cycleNodes)
        {
            if (!joined.empty())
                joined += ", ";
            joined += std::to_string(ts);
        }
        return std::format("Dependency cycle detected among pending migrations: {}", joined);
    }

    [[nodiscard]] MigrationManager::MigrationList KahnOrder(PendingDependencyGraph graph, size_t expectedSize)
    {
        auto ready = std::vector<uint64_t> {};
        for (auto const& [ts, deg]: graph.inDegree)
            if (deg == 0)
                ready.push_back(ts);
        std::ranges::sort(ready);

        auto result = MigrationManager::MigrationList {};
        while (!ready.empty())
        {
            auto const ts = ready.front();
            ready.erase(ready.begin());
            result.push_back(graph.pendingByTs[ts]);

            auto const it = graph.dependents.find(ts);
            if (it == graph.dependents.end())
                continue;
            for (auto const& dependent: it->second)
            {
                auto& deg = graph.inDegree[dependent];
                --deg;
                if (deg == 0)
                {
                    auto const pos = std::ranges::lower_bound(ready, dependent);
                    ready.insert(pos, dependent);
                }
            }
        }

        if (result.size() != expectedSize)
            throw std::runtime_error(FormatCycleError(graph.inDegree));

        return result;
    }
} // namespace

MigrationManager::MigrationList MigrationManager::TopoSortPending(MigrationList pending,
                                                                  std::vector<MigrationTimestamp> const& applied) const
{
    if (!AnyPendingHasDependencies(pending))
        return pending;

    auto const appliedSet = ToTimestampSet(applied);
    auto const registeredSet = ToTimestampSet(_migrations);
    auto graph = BuildPendingGraph(pending, appliedSet, registeredSet);

    return KahnOrder(std::move(graph), pending.size());
}

void MigrationManager::ValidateDependencies() const
{
    // Delegates to TopoSortPending, which throws on unknown deps or cycles.
    auto const applied = GetAppliedMigrationIds();
    auto pending = MigrationList {};
    for (auto const* migration: _migrations)
        if (!std::ranges::contains(applied, migration->GetTimestamp()))
            pending.push_back(migration);
    (void) TopoSortPending(std::move(pending), applied);
}

void MigrationManager::RegisterRelease(std::string version, MigrationTimestamp highestTimestamp)
{
    // Reject duplicate version strings.
    auto const byVersion = std::ranges::find_if(_releases, [&](MigrationRelease const& r) { return r.version == version; });
    if (byVersion != _releases.end())
    {
        throw std::runtime_error(
            std::format("Duplicate release registration for version '{}' (existing timestamp {}, new timestamp {}).",
                        version,
                        byVersion->highestTimestamp.value,
                        highestTimestamp.value));
    }

    // Reject duplicate timestamps — two releases cannot share the same cut-point.
    auto const byTimestamp =
        std::ranges::find_if(_releases, [&](MigrationRelease const& r) { return r.highestTimestamp == highestTimestamp; });
    if (byTimestamp != _releases.end())
    {
        throw std::runtime_error(std::format("Duplicate release timestamp {}: '{}' conflicts with existing release '{}'.",
                                             highestTimestamp.value,
                                             version,
                                             byTimestamp->version));
    }

    _releases.emplace_back(MigrationRelease { .version = std::move(version), .highestTimestamp = highestTimestamp });
    std::ranges::sort(_releases, [](MigrationRelease const& a, MigrationRelease const& b) {
        return a.highestTimestamp < b.highestTimestamp;
    });
}

void MigrationManager::RemoveAllReleases()
{
    _releases.clear();
}

std::vector<MigrationRelease> const& MigrationManager::GetAllReleases() const noexcept
{
    return _releases;
}

MigrationRelease const* MigrationManager::FindReleaseByVersion(std::string_view version) const noexcept
{
    auto const it = std::ranges::find_if(_releases, [&](MigrationRelease const& r) { return r.version == version; });
    return it != _releases.end() ? &*it : nullptr;
}

MigrationRelease const* MigrationManager::FindReleaseForTimestamp(MigrationTimestamp timestamp) const noexcept
{
    // _releases is sorted ascending by highestTimestamp. Return the first release whose
    // highestTimestamp covers `timestamp`.
    auto const it = std::ranges::lower_bound(_releases, timestamp, {}, &MigrationRelease::highestTimestamp);
    return it != _releases.end() ? &*it : nullptr;
}

MigrationManager::MigrationList MigrationManager::GetMigrationsForRelease(std::string_view version) const
{
    auto const* target = FindReleaseByVersion(version);
    if (!target)
        return {};

    // Determine the previous release's highestTimestamp as an exclusive lower bound.
    MigrationTimestamp prev { 0 };
    for (auto const& r: _releases)
    {
        if (r.highestTimestamp < target->highestTimestamp && r.highestTimestamp > prev)
            prev = r.highestTimestamp;
    }

    MigrationList result;
    for (auto const* migration: _migrations)
    {
        auto const ts = migration->GetTimestamp();
        if (ts > prev && ts <= target->highestTimestamp)
            result.push_back(migration);
    }
    return result;
}

namespace
{
    /// Represents a parsed SQLite runtime guard extracted from a SQL script's leading sentinel comment.
    ///
    /// The SQLite formatter emits guarded DDL with a marker:
    /// `-- LIGHTWEIGHT_SQLITE_GUARD: <KIND> "<table>" "<column>"\n<DDL>`
    /// This struct captures the parsed shape for runtime presence-check handling.
    struct SqliteGuard
    {
        enum class Kind : uint8_t
        {
            AddColumnIfNotExists,
            DropColumnIfExists,
            AddForeignKey,
            DropForeignKey,
            AddCompositeForeignKey,
            AlterColumn,
        };

        Kind kind;
        std::string tableName;
        std::string columnName;
        // Only populated for Kind::AddForeignKey. Empty otherwise.
        std::string referencedTable;
        std::string referencedColumn;
        // Only populated for Kind::AddCompositeForeignKey. Empty otherwise.
        std::vector<std::string> columns;
        std::vector<std::string> referencedColumns;
        // Only populated for Kind::AlterColumn. Empty / false otherwise.
        std::string columnType;
        bool notNull = false;
    };

    [[nodiscard]] std::optional<SqliteGuard::Kind> ParseSqliteGuardKind(std::string_view kindStr)
    {
        if (kindStr == "ADD_COLUMN_IF_NOT_EXISTS")
            return SqliteGuard::Kind::AddColumnIfNotExists;
        if (kindStr == "DROP_COLUMN_IF_EXISTS")
            return SqliteGuard::Kind::DropColumnIfExists;
        if (kindStr == "ADD_FOREIGN_KEY")
            return SqliteGuard::Kind::AddForeignKey;
        if (kindStr == "DROP_FOREIGN_KEY")
            return SqliteGuard::Kind::DropForeignKey;
        if (kindStr == "ADD_COMPOSITE_FOREIGN_KEY")
            return SqliteGuard::Kind::AddCompositeForeignKey;
        if (kindStr == "ALTER_COLUMN")
            return SqliteGuard::Kind::AlterColumn;
        return std::nullopt;
    }

    /// Extract @p expectedStrings double-quoted fields from a sentinel directive, starting at
    /// @p searchPos. A doubled `""` inside a field is treated as an escaped quote and decoded to a
    /// single `"`, so identifiers containing a double-quote survive the round-trip (the formatter
    /// escapes them the same way). Returns the decoded field values, or `std::nullopt` if fewer than
    /// @p expectedStrings well-formed fields are present.
    [[nodiscard]] std::optional<std::vector<std::string>> ExtractQuotedStrings(std::string_view directive,
                                                                               size_t searchPos,
                                                                               size_t expectedStrings)
    {
        std::vector<std::string> quoted;
        quoted.reserve(expectedStrings);
        while (quoted.size() < expectedStrings)
        {
            auto const openQuote = directive.find('"', searchPos);
            if (openQuote == std::string_view::npos)
                return std::nullopt;
            std::string value;
            size_t scan = openQuote + 1;
            bool closed = false;
            while (scan < directive.size())
            {
                if (directive[scan] == '"')
                {
                    if (scan + 1 < directive.size() && directive[scan + 1] == '"')
                    {
                        value += '"'; // doubled-quote escape inside the field
                        scan += 2;
                        continue;
                    }
                    closed = true;
                    break;
                }
                value += directive[scan];
                ++scan;
            }
            if (!closed)
                return std::nullopt;
            quoted.push_back(std::move(value));
            searchPos = scan + 1;
        }
        return quoted;
    }

    [[nodiscard]] std::vector<std::string> SplitCommaList(std::string_view list)
    {
        std::vector<std::string> result;
        size_t begin = 0;
        while (begin <= list.size())
        {
            auto const comma = list.find(',', begin);
            auto const end = comma == std::string_view::npos ? list.size() : comma;
            result.emplace_back(list.substr(begin, end - begin));
            if (comma == std::string_view::npos)
                break;
            begin = comma + 1;
        }
        return result;
    }

    [[nodiscard]] SqliteGuard BuildSqliteGuard(SqliteGuard::Kind kind, std::span<std::string const> quoted)
    {
        SqliteGuard guard {
            .kind = kind,
            .tableName = std::string { quoted[0] },
            .columnName = std::string { quoted[1] },
            .referencedTable = {},
            .referencedColumn = {},
            .columns = {},
            .referencedColumns = {},
            .columnType = {},
            .notNull = false,
        };
        if (kind == SqliteGuard::Kind::AddForeignKey)
        {
            guard.referencedTable = std::string { quoted[2] };
            guard.referencedColumn = std::string { quoted[3] };
        }
        else if (kind == SqliteGuard::Kind::AddCompositeForeignKey)
        {
            guard.columnName.clear(); // scalar field unused for composite kind
            guard.columns = SplitCommaList(quoted[1]);
            guard.referencedTable = std::string { quoted[2] };
            guard.referencedColumns = SplitCommaList(quoted[3]);
        }
        else if (kind == SqliteGuard::Kind::AlterColumn)
        {
            guard.columnType = std::string { quoted[2] };
            guard.notNull = quoted[3] == "NOT NULL";
        }
        return guard;
    }

    /// Parse the leading sentinel (if any) from a SQL script.
    ///
    /// @param script The SQL script possibly starting with a `-- LIGHTWEIGHT_SQLITE_GUARD: ...` line.
    /// @return The parsed guard and position after the sentinel line, or std::nullopt if absent.
    [[nodiscard]] std::optional<std::pair<SqliteGuard, size_t>> TryParseSqliteGuard(std::string_view script)
    {
        constexpr auto marker = std::string_view { "-- LIGHTWEIGHT_SQLITE_GUARD:" };
        if (!script.starts_with(marker))
            return std::nullopt;

        auto const newlinePos = script.find('\n');
        if (newlinePos == std::string_view::npos)
            return std::nullopt;

        auto const directive = script.substr(marker.size(), newlinePos - marker.size());

        // Expected form: ` <KIND> "<table>" "<column>"`
        auto const kindStart = directive.find_first_not_of(' ');
        if (kindStart == std::string_view::npos)
            return std::nullopt;
        auto const kindEnd = directive.find(' ', kindStart);
        if (kindEnd == std::string_view::npos)
            return std::nullopt;

        auto const kind = ParseSqliteGuardKind(directive.substr(kindStart, kindEnd - kindStart));
        if (!kind)
            return std::nullopt;

        // Parse N quoted identifiers after the kind keyword. Single-column kinds take
        // exactly 2 quoted args (table, column); AddForeignKey takes 4 (adds referenced
        // table, referenced column); AddCompositeForeignKey also takes 4 but the 2nd
        // and 4th are comma-joined column lists split by the consumer; AlterColumn takes
        // 4 (adds the new column type and the NULL/NOT NULL token).
        size_t const expectedStrings =
            (*kind == SqliteGuard::Kind::AddForeignKey || *kind == SqliteGuard::Kind::AddCompositeForeignKey
             || *kind == SqliteGuard::Kind::AlterColumn)
                ? 4
                : 2;
        auto const quoted = ExtractQuotedStrings(directive, kindEnd, expectedStrings);
        if (!quoted)
            return std::nullopt;

        return std::pair { BuildSqliteGuard(*kind, *quoted), newlinePos + 1 };
    }

    /// Check whether a column exists on a SQLite table via pragma_table_info().
    ///
    /// Delegates query construction to @ref SQLiteQueryFormatter::BuildColumnExistsQuery so the
    /// sentinel-emitting formatter and the runtime check share a single SQL definition.
    [[nodiscard]] bool SqliteColumnExists(SqlConnection& connection, std::string_view tableName, std::string_view columnName)
    {
        auto stmt = SqlStatement { connection };
        auto cursor = stmt.ExecuteDirect(SQLiteQueryFormatter::BuildColumnExistsQuery(tableName, columnName));
        if (!cursor.FetchRow())
            return false;
        return cursor.GetColumn<int64_t>(1) > 0;
    }

    /// @brief Escape a value for embedding inside a single-quoted SQL string literal (doubles `'`).
    /// @param value The raw value (e.g. a table name) to embed.
    /// @return The escaped text, to be placed between the surrounding single quotes.
    [[nodiscard]] std::string EscapeSqlStringLiteral(std::string_view value)
    {
        std::string out;
        out.reserve(value.size());
        for (auto const ch: value)
        {
            out += ch;
            if (ch == '\'')
                out += '\'';
        }
        return out;
    }

    /// @brief Escape a value for embedding inside a double-quoted SQL identifier (doubles `"`).
    /// @param value The raw identifier (e.g. a table name) to embed.
    /// @return The escaped text, to be placed between the surrounding double quotes.
    [[nodiscard]] std::string EscapeSqlIdentifier(std::string_view value)
    {
        std::string out;
        out.reserve(value.size());
        for (auto const ch: value)
        {
            out += ch;
            if (ch == '"')
                out += '"';
        }
        return out;
    }

    /// Fetch the stored `CREATE TABLE` SQL for a SQLite table.
    [[nodiscard]] std::string FetchSqliteCreateTableSql(SqlStatement& stmt, std::string_view tableName)
    {
        auto cursor = stmt.ExecuteDirect(std::format(R"(SELECT sql FROM sqlite_schema WHERE type='table' AND name='{}')",
                                                     EscapeSqlStringLiteral(tableName)));
        if (!cursor.FetchRow())
            throw std::runtime_error(std::format("SQLite rebuild: table '{}' not found in sqlite_schema", tableName));
        return cursor.GetColumn<std::string>(1);
    }

    /// Fetch the insertable column names of a SQLite table in declared order.
    ///
    /// Uses `table_xinfo` so generated columns can be excluded: its `hidden` flag is 0 for an ordinary
    /// column, 1 for a hidden column, and 2/3 for VIRTUAL/STORED generated columns. Only ordinary
    /// columns may appear in an `INSERT`, so the rebuild's data-copy column list must skip the rest.
    [[nodiscard]] std::vector<std::string> FetchSqliteColumnNames(SqlStatement& stmt, std::string_view tableName)
    {
        auto cursor = stmt.ExecuteDirect(std::format(R"(PRAGMA table_xinfo("{}"))", EscapeSqlIdentifier(tableName)));
        std::vector<std::string> columns;
        while (cursor.FetchRow())
            // table_xinfo columns: 1=cid 2=name 3=type 4=notnull 5=dflt_value 6=pk 7=hidden.
            if (cursor.GetColumn<int64_t>(7) == 0) // ordinary (insertable) column
                columns.push_back(cursor.GetColumn<std::string>(2));
        return columns;
    }

    /// @brief Fetch the DDL of explicitly-created indexes and triggers attached to a SQLite table.
    ///
    /// These live in their own `sqlite_schema` rows and are destroyed by `DROP TABLE`, so a table
    /// rebuild must re-create them afterwards. Auto-indexes that back inline `UNIQUE` / `PRIMARY KEY`
    /// constraints have a NULL `sql` and are excluded — SQLite recreates those with the table itself.
    /// @param stmt A statement bound to the connection being rebuilt.
    /// @param tableName The table whose dependent objects to collect.
    /// @return The `CREATE INDEX` / `CREATE TRIGGER` statements in `sqlite_schema` order.
    [[nodiscard]] std::vector<std::string> FetchSqliteDependentObjectsSql(SqlStatement& stmt, std::string_view tableName)
    {
        auto cursor = stmt.ExecuteDirect(std::format(
            R"(SELECT sql FROM sqlite_schema WHERE tbl_name = '{}' AND type IN ('index', 'trigger') AND sql IS NOT NULL)",
            EscapeSqlStringLiteral(tableName)));
        std::vector<std::string> dependentObjectsSql;
        while (cursor.FetchRow())
            dependentObjectsSql.push_back(cursor.GetColumn<std::string>(1));
        return dependentObjectsSql;
    }

    /// Rebuild a SQLite table while transforming its stored `CREATE TABLE` SQL.
    ///
    /// The table rebuild follows the SQLite-recommended recipe:
    ///   1. Build a modified `CREATE TABLE` pointing at a temp-named table.
    ///   2. Copy data: `INSERT INTO tmp SELECT cols FROM orig`.
    ///   3. `DROP TABLE orig`.
    ///   4. `ALTER TABLE tmp RENAME TO orig`.
    ///
    /// The caller supplies `transformCreateSql` which receives the original SQL (with the
    /// table name already substituted for the temp name) and returns the final CREATE TABLE
    /// text. Explicitly-created indexes and triggers are captured beforehand and re-created
    /// afterwards. The migration transaction covers every step for atomicity.
    ///
    /// Foreign-key enforcement is NOT toggled here: SQLite only honours `PRAGMA foreign_keys`
    /// outside a transaction, and migrations run inside one. The caller must therefore ensure
    /// `foreign_keys = OFF` before migrating a table that other tables reference — with enforcement
    /// on, the `DROP TABLE` can fire cascade actions or leave another table's FK dangling. (The
    /// proper fix is to disable `foreign_keys` at the migration-runner level, before the
    /// transaction is opened, gated on SQLite.)
    void RebuildSqliteTable(SqlConnection& connection, std::string_view tableName, auto&& transformCreateSql)
    {
        auto stmt = SqlStatement { connection };
        auto const originalSql = FetchSqliteCreateTableSql(stmt, tableName);
        auto const columns = FetchSqliteColumnNames(stmt, tableName);
        if (columns.empty())
            throw std::runtime_error(std::format("SQLite rebuild: table '{}' has no columns", tableName));

        // Capture indexes/triggers before the drop destroys them; they are re-created after RENAME.
        auto const dependentObjectsSql = FetchSqliteDependentObjectsSql(stmt, tableName);

        std::string const tmpName = std::string { tableName } + "__lw_rebuild";

        // Substitute the original table name with the temp name in the stored SQL. Prefer the quoted
        // form Lightweight emits. For tables created outside the library, fall back to a *word-boundary*
        // match of the bare name so a short name (e.g. "T", a substring of "CREATE"/"TABLE") is not
        // spliced into a keyword, a column name, or a self-referential REFERENCES clause. Only the first
        // matching occurrence — the table declaration — is replaced.
        auto const isIdentifierChar = [](char c) noexcept {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        };
        auto replaceFirstWord = [&isIdentifierChar](std::string s, std::string_view word, std::string_view to) {
            size_t from = 0;
            while ((from = s.find(word, from)) != std::string::npos)
            {
                bool const leftOk = from == 0 || !isIdentifierChar(s[from - 1]);
                size_t const after = from + word.size();
                bool const rightOk = after >= s.size() || !isIdentifierChar(s[after]);
                if (leftOk && rightOk)
                {
                    s.replace(from, word.size(), to);
                    break;
                }
                from = after;
            }
            return s;
        };
        auto const quotedName = std::format(R"("{}")", tableName);
        std::string withTmpName = originalSql;
        if (auto const pos = withTmpName.find(quotedName); pos != std::string::npos)
            withTmpName.replace(pos, quotedName.size(), std::format(R"("{}")", tmpName));
        else // unquoted fallback for externally-created tables
            withTmpName = replaceFirstWord(originalSql, tableName, tmpName);

        auto const newSql = transformCreateSql(std::move(withTmpName));

        auto exec = [&](std::string const& sql, std::string_view step) {
            try
            {
                (void) stmt.ExecuteDirect(sql);
            }
            catch (SqlException const& ex)
            {
                auto const& info = ex.info();
                throw SqlException(SqlErrorInfo {
                    .nativeErrorCode = info.nativeErrorCode,
                    .sqlState = info.sqlState,
                    .message = std::format(
                        "SQLite rebuild of '{}' failed at {}: {}\n  SQL: {}", tableName, step, info.message, sql),
                });
            }
        };

        // Clear any leftover temp table from a previously-failed rebuild so the CREATE can't collide.
        exec(std::format(R"(DROP TABLE IF EXISTS "{}")", tmpName), "DROP stale tmp");
        exec(newSql, "CREATE TABLE (tmp)");

        std::string columnList;
        for (size_t i = 0; i < columns.size(); ++i)
        {
            if (i != 0)
                columnList += ", ";
            columnList += std::format(R"("{}")", columns[i]);
        }
        exec(std::format(R"(INSERT INTO "{}" ({}) SELECT {} FROM "{}")", tmpName, columnList, columnList, tableName),
             "INSERT SELECT");
        exec(std::format(R"(DROP TABLE "{}")", tableName), "DROP TABLE");
        exec(std::format(R"(ALTER TABLE "{}" RENAME TO "{}")", tmpName, tableName), "ALTER TABLE RENAME");

        // Re-create the indexes and triggers dropped with the original table. Their stored DDL
        // names the table by its (now restored) original name, so it applies unchanged.
        for (auto const& dependentObjectSql: dependentObjectsSql)
            exec(dependentObjectSql, "recreate dependent object");
    }

    /// Rebuild a SQLite table to add a new foreign key constraint.
    void SqliteRebuildAddForeignKey(SqlConnection& connection, SqliteGuard const& guard)
    {
        auto const fkName = SqlQueryFormatter::BuildForeignKeyConstraintName(
            guard.tableName, std::array { std::string_view { guard.columnName } });
        auto const fk = std::format(R"(CONSTRAINT "{0}" FOREIGN KEY ("{1}") REFERENCES "{2}"("{3}"))",
                                    fkName,
                                    guard.columnName,
                                    guard.referencedTable,
                                    guard.referencedColumn);

        RebuildSqliteTable(connection, guard.tableName, [&](std::string createSql) {
            auto const closeParen = createSql.rfind(')');
            if (closeParen == std::string::npos)
                throw std::runtime_error(
                    std::format("SQLite rebuild: cannot find closing ')' in CREATE TABLE for '{}'", guard.tableName));
            createSql.insert(closeParen, ", " + fk);
            return createSql;
        });
    }

    /// Rebuild a SQLite table to add a new composite foreign key constraint.
    ///
    /// Mirrors `SqliteRebuildAddForeignKey` but emits a multi-column FOREIGN KEY clause.
    /// The constraint name is shared with the CREATE TABLE path
    /// (`SqlQueryFormatter::BuildForeignKeyConstraintName`) so DROP lookups remain consistent.
    void SqliteRebuildAddCompositeForeignKey(SqlConnection& connection, SqliteGuard const& guard)
    {
        auto const joinQuoted = [](std::vector<std::string> const& v) {
            std::string out;
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (i != 0)
                    out += ", ";
                out += '"';
                out += v[i];
                out += '"';
            }
            return out;
        };
        auto const fkName = SqlQueryFormatter::BuildForeignKeyConstraintName(guard.tableName, guard.columns);
        auto const fk = std::format(R"(CONSTRAINT "{0}" FOREIGN KEY ({1}) REFERENCES "{2}"({3}))",
                                    fkName,
                                    joinQuoted(guard.columns),
                                    guard.referencedTable,
                                    joinQuoted(guard.referencedColumns));

        RebuildSqliteTable(connection, guard.tableName, [&](std::string createSql) {
            auto const closeParen = createSql.rfind(')');
            if (closeParen == std::string::npos)
                throw std::runtime_error(
                    std::format("SQLite rebuild: cannot find closing ')' in CREATE TABLE for '{}'", guard.tableName));
            createSql.insert(closeParen, ", " + fk);
            return createSql;
        });
    }

    /// @brief Advance past a single-quoted SQL string literal.
    /// Handles doubled-quote (`''`) escapes. If @p from is not at a quote, returns it unchanged so
    /// the caller can keep scanning ordinary text.
    /// @param s The text being scanned.
    /// @param from Index to inspect.
    /// @return Index just past the literal's closing quote, or `s.size()` if it is unterminated.
    [[nodiscard]] size_t SkipStringLiteral(std::string_view s, size_t from) noexcept
    {
        if (from >= s.size() || s[from] != '\'')
            return from;
        size_t scan = from + 1;
        while (scan < s.size())
        {
            if (s[scan] == '\'')
            {
                if (scan + 1 < s.size() && s[scan + 1] == '\'')
                {
                    scan += 2; // doubled-quote escape inside the literal
                    continue;
                }
                return scan + 1;
            }
            ++scan;
        }
        return s.size();
    }

    /// @brief Advance past a double-quoted SQL identifier (`"..."`), honouring `""` escapes.
    /// If @p from is not at a double quote, returns it unchanged so the caller can keep scanning.
    /// Quoted identifiers can legally contain `'`, `(`, `)`, and `,`, so treating them as opaque keeps
    /// those characters from being misread as string-literal, paren, or column-list structure.
    /// @param s The text being scanned.
    /// @param from Index to inspect.
    /// @return Index just past the identifier's closing quote, or `s.size()` if it is unterminated.
    [[nodiscard]] size_t SkipQuotedIdentifier(std::string_view s, size_t from) noexcept
    {
        if (from >= s.size() || s[from] != '"')
            return from;
        size_t scan = from + 1;
        while (scan < s.size())
        {
            if (s[scan] == '"')
            {
                if (scan + 1 < s.size() && s[scan + 1] == '"')
                {
                    scan += 2; // doubled-quote escape inside the identifier
                    continue;
                }
                return scan + 1;
            }
            ++scan;
        }
        return s.size();
    }

    /// @brief Decode a double-quoted SQL identifier into its raw name (collapsing `""` to `"`).
    /// @param s The text containing the identifier.
    /// @param openQuote Index of the opening `"`.
    /// @param pastClose Index just past the closing `"` (as returned by @ref SkipQuotedIdentifier).
    /// @return The unquoted, unescaped identifier text.
    [[nodiscard]] std::string UnquoteSqlIdentifier(std::string_view s, size_t openQuote, size_t pastClose)
    {
        std::string out;
        size_t const closeQuote = pastClose - 1; // index of the closing '"'
        size_t k = openQuote + 1;
        while (k < closeQuote)
        {
            if (s[k] == '"' && k + 1 < closeQuote && s[k + 1] == '"')
            {
                out += '"';
                k += 2;
            }
            else
                out += s[k++];
        }
        return out;
    }

    /// Advance past a `(...)` group starting at the first `(` at or after `from`.
    /// Single-quoted string literals and double-quoted identifiers inside the group are skipped whole
    /// so a `)` within a literal or identifier cannot unbalance the depth count.
    /// Returns the index just past the matching close paren, or `std::string::npos`
    /// if the text is malformed.
    [[nodiscard]] size_t SkipMatchingParens(std::string_view s, size_t from)
    {
        auto const open = s.find('(', from);
        if (open == std::string_view::npos)
            return std::string_view::npos;
        int depth = 1;
        size_t scan = open + 1;
        while (scan < s.size() && depth > 0)
        {
            if (s[scan] == '\'')
            {
                scan = SkipStringLiteral(s, scan);
                continue;
            }
            if (s[scan] == '"')
            {
                scan = SkipQuotedIdentifier(s, scan);
                continue;
            }
            if (s[scan] == '(')
                ++depth;
            else if (s[scan] == ')')
                --depth;
            ++scan;
        }
        return depth == 0 ? scan : std::string_view::npos;
    }

    /// @brief Find the next whitespace-delimited token in @p s starting at @p from, treating
    /// parenthesised groups, single-quoted string literals, and double-quoted identifiers as opaque
    /// (so commas, parens, quotes, and keywords inside them are never split out).
    /// @param s The text being tokenised.
    /// @param from Index to start scanning from.
    /// @return The token's `[start, end)` range; an empty range `{s.size(), s.size()}` when none remains.
    [[nodiscard]] std::pair<size_t, size_t> NextSqlToken(std::string_view s, size_t from)
    {
        size_t i = from;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n'))
            ++i;
        if (i >= s.size())
            return { s.size(), s.size() };
        size_t const start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t' && s[i] != '\n')
        {
            if (s[i] == '(')
            {
                auto const end = SkipMatchingParens(s, i);
                i = end == std::string_view::npos ? s.size() : end;
            }
            else if (s[i] == '\'')
                i = SkipStringLiteral(s, i);
            else if (s[i] == '"')
                i = SkipQuotedIdentifier(s, i);
            else
                ++i;
        }
        return { start, i };
    }

    /// Expand `[start..pos)` backwards to include adjacent whitespace and up to one
    /// trailing comma — so erasing `[newStart..end)` from a column list also removes
    /// the separator preceding the clause.
    [[nodiscard]] size_t ExtendLeftPastSeparator(std::string_view s, size_t pos)
    {
        auto start = pos;
        while (start > 0 && (s[start - 1] == ' ' || s[start - 1] == '\t' || s[start - 1] == '\n'))
            --start;
        if (start > 0 && s[start - 1] == ',')
            --start;
        return start;
    }

    /// Locate the FK clause to drop. Tries (in order):
    ///   1. `CONSTRAINT "<fkName>"`
    ///   2. `CONSTRAINT <fkName>` (unquoted)
    ///   3. Bare `FOREIGN KEY ("<column>")` — SQLite's stored CREATE TABLE often omits
    ///      the `CONSTRAINT <name>` prefix because Lightweight's SQLite formatter emits
    ///      FKs inline without naming them.
    [[nodiscard]] size_t FindForeignKeyClause(std::string_view sql, std::string_view fkName, std::string_view columnName)
    {
        if (auto const pos = sql.find(std::format(R"(CONSTRAINT "{}")", fkName)); pos != std::string_view::npos)
            return pos;
        if (auto const pos = sql.find(std::format(R"(CONSTRAINT {})", fkName)); pos != std::string_view::npos)
            return pos;
        return sql.find(std::format(R"(FOREIGN KEY ("{}"))", columnName));
    }

    /// Rebuild a SQLite table to drop a foreign key constraint.
    ///
    /// Strips `CONSTRAINT "FK_<table>_<column>" FOREIGN KEY (…) REFERENCES "T"("C")` —
    /// or the unquoted variant — along with its leading comma-and-whitespace.
    void SqliteRebuildDropForeignKey(SqlConnection& connection, SqliteGuard const& guard)
    {
        auto const fkName = SqlQueryFormatter::BuildForeignKeyConstraintName(
            guard.tableName, std::array { std::string_view { guard.columnName } });

        RebuildSqliteTable(connection, guard.tableName, [&](std::string createSql) {
            auto const pos = FindForeignKeyClause(createSql, fkName, guard.columnName);
            if (pos == std::string::npos)
                throw std::runtime_error(std::format(
                    "SQLite rebuild: cannot locate FK for '{}.{}' in CREATE TABLE", guard.tableName, guard.columnName));

            // Skip the FOREIGN KEY (...) list, then the REFERENCES table(col) list.
            auto scan = SkipMatchingParens(createSql, pos);
            if (scan == std::string::npos)
                throw std::runtime_error("SQLite rebuild: malformed FOREIGN KEY paren group");
            scan = SkipMatchingParens(createSql, scan);
            if (scan == std::string::npos)
                throw std::runtime_error("SQLite rebuild: malformed REFERENCES paren group");

            auto const start = ExtendLeftPastSeparator(createSql, pos);
            createSql.erase(start, scan - start);
            return createSql;
        });
    }

    /// @brief ASCII-fold a character to upper case (locale-independent — SQL keywords are ASCII).
    [[nodiscard]] constexpr char AsciiFold(char c) noexcept
    {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
    }

    /// @brief ASCII case-insensitive string equality, without allocating.
    /// @param a First string.
    /// @param b Second string.
    /// @return True iff @p a and @p b are equal ignoring ASCII letter case.
    [[nodiscard]] bool IEqualsAscii(std::string_view a, std::string_view b) noexcept
    {
        return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) { return AsciiFold(x) == AsciiFold(y); });
    }

    /// @brief Whether @p text contains @p keyword as a standalone ASCII case-insensitive token.
    /// Unlike a substring search this only matches a real SQL token: @ref NextSqlToken treats
    /// parenthesised groups, string literals, and quoted identifiers as opaque, so the keyword is
    /// never matched inside a `DEFAULT 'value'`, a `CHECK(...)` expression, or an identifier.
    /// @param text The text to search.
    /// @param keyword The keyword to look for.
    /// @return True iff @p keyword occurs in @p text as a token, ignoring ASCII letter case.
    [[nodiscard]] bool ContainsTokenAscii(std::string_view text, std::string_view keyword)
    {
        size_t scan = 0;
        while (true)
        {
            auto const [start, end] = NextSqlToken(text, scan);
            if (start == end)
                return false;
            if (IEqualsAscii(text.substr(start, end - start), keyword))
                return true;
            scan = end;
        }
    }

    /// @brief Drop the nullability keywords (`NOT NULL`, standalone `NULL`) from a column's inline
    /// constraint list so a fresh nullability can be applied. A `NULL` that belongs to a
    /// `DEFAULT NULL` clause is normally preserved; when @p makingNotNull is set the now contradictory
    /// default is dropped as well, in all three spellings: `DEFAULT NULL`, `DEFAULT (NULL)`, and the
    /// space-less `DEFAULT(NULL)` (which the tokeniser collapses into a single token). Parenthesised
    /// groups (`CHECK(...)`), single-quoted string literals, and double-quoted identifiers are treated
    /// as opaque so commas, quotes, and keywords inside them are never misread.
    /// @param constraints The inline constraint text following a column's type token.
    /// @param makingNotNull Whether the column is being changed to `NOT NULL`.
    /// @return The constraint text with the nullability keywords removed.
    ///
    /// @note SQLite necessarily reconstructs the whole column here, so it normalises a contradictory
    /// `DEFAULT NULL` away. The in-place `ALTER COLUMN` emitted for PostgreSQL / SQL Server leaves the
    /// existing default untouched; that cross-dialect difference is intentional — only SQLite has to
    /// re-render the column definition from scratch.
    [[nodiscard]] std::string StripNullabilityKeywords(std::string_view constraints, bool makingNotNull)
    {
        std::vector<std::string_view> tokens; // views into `constraints` — no per-token allocation
        size_t scan = 0;
        while (true)
        {
            auto const [start, end] = NextSqlToken(constraints, scan);
            if (start == end)
                break;
            tokens.push_back(constraints.substr(start, end - start));
            scan = end;
        }

        auto const isNullToken = [](std::string_view tok) {
            return IEqualsAscii(tok, "NULL") || IEqualsAscii(tok, "(NULL)");
        };
        // A `DEFAULT(NULL)` written without a space is collapsed into one token by NextSqlToken.
        auto const isDefaultNullToken = [](std::string_view tok) {
            return IEqualsAscii(tok, "DEFAULT(NULL)");
        };

        std::vector<std::string_view> kept;
        kept.reserve(tokens.size());
        size_t t = 0;
        while (t < tokens.size())
        {
            if (IEqualsAscii(tokens[t], "NOT") && t + 1 < tokens.size() && IEqualsAscii(tokens[t + 1], "NULL"))
            {
                t += 2; // skip `NOT NULL`
                continue;
            }
            if (makingNotNull && IEqualsAscii(tokens[t], "DEFAULT") && t + 1 < tokens.size() && isNullToken(tokens[t + 1]))
            {
                t += 2; // a NULL default contradicts NOT NULL — drop the whole `DEFAULT NULL` clause
                continue;
            }
            if (makingNotNull && isDefaultNullToken(tokens[t]))
            {
                ++t; // same, for the space-less `DEFAULT(NULL)` spelling
                continue;
            }
            if (IEqualsAscii(tokens[t], "NULL") && (kept.empty() || !IEqualsAscii(kept.back(), "DEFAULT")))
            {
                ++t;
                continue;
            }
            kept.push_back(tokens[t]);
            ++t;
        }

        std::string out;
        for (auto const& token: kept)
        {
            if (!out.empty())
                out += ' ';
            out += token;
        }
        return out;
    }

    /// @brief Find the byte offset of the column-list opening parenthesis in a `CREATE TABLE` statement.
    /// Scans top-level text only, skipping quoted identifiers and string literals, so a `(` inside a
    /// quoted name or a string literal is never mistaken for the column-list opener.
    /// @param createSql The stored `CREATE TABLE` text.
    /// @return The index of the `(` that opens the column list, or `std::string::npos` if none exists.
    [[nodiscard]] size_t FindColumnListOpen(std::string_view createSql) noexcept
    {
        size_t pos = 0;
        while (pos < createSql.size())
        {
            char const ch = createSql[pos];
            if (ch == '"')
                pos = SkipQuotedIdentifier(createSql, pos);
            else if (ch == '\'')
                pos = SkipStringLiteral(createSql, pos);
            else if (ch == '(')
                return pos;
            else
                ++pos;
        }
        return std::string::npos;
    }

    /// @brief Test whether a column-list entry defines a given column, i.e. its first
    /// whitespace-skipped token is the quoted column name.
    /// @param createSql The stored `CREATE TABLE` text.
    /// @param entryStart Index of the entry's first character.
    /// @param entryEnd Index just past the entry (its terminating comma or the list's `)`).
    /// @param columnName The column name to match.
    /// @return The index just past the entry's quoted name when it defines @p columnName; otherwise
    /// `std::string::npos`.
    [[nodiscard]] size_t MatchColumnEntryName(std::string_view createSql,
                                              size_t entryStart,
                                              size_t entryEnd,
                                              std::string_view columnName)
    {
        size_t nameStart = entryStart;
        while (nameStart < entryEnd
               && (createSql[nameStart] == ' ' || createSql[nameStart] == '\t' || createSql[nameStart] == '\n'))
            ++nameStart;
        if (nameStart >= entryEnd || createSql[nameStart] != '"')
            return std::string::npos;
        size_t const nameEnd = SkipQuotedIdentifier(createSql, nameStart);
        if (nameEnd <= entryEnd && UnquoteSqlIdentifier(createSql, nameStart, nameEnd) == columnName)
            return nameEnd;
        return std::string::npos;
    }

    /// @brief Locate a column's definition as a top-level entry of a `CREATE TABLE` column list.
    /// The list entries are separated by top-level commas; a column definition is an entry whose first
    /// token is the quoted column name. Nested parens, string literals, and quoted identifiers are
    /// skipped so the match never lands on a `CHECK("name" > 0)`, a `REFERENCES "T"("name")`, or a
    /// `DEFAULT '... "name" ...'` that merely mentions the name.
    /// @param createSql The stored `CREATE TABLE` text.
    /// @param listOpen The index of the column list's opening `(` (see @ref FindColumnListOpen).
    /// @param columnName The column to locate.
    /// @return `{afterName, defEnd}` — the index just past the column's quoted name and the index of
    /// the entry's terminating comma or the list's `)`; `{npos, npos}` if the column is not found.
    [[nodiscard]] std::pair<size_t, size_t> LocateColumnDefinition(std::string_view createSql,
                                                                   size_t listOpen,
                                                                   std::string_view columnName)
    {
        size_t entryStart = listOpen + 1;
        size_t pos = entryStart;
        int depth = 0;
        while (pos < createSql.size())
        {
            char const ch = createSql[pos];
            if (ch == '"')
            {
                pos = SkipQuotedIdentifier(createSql, pos);
                continue;
            }
            if (ch == '\'')
            {
                pos = SkipStringLiteral(createSql, pos);
                continue;
            }
            if (ch == '(')
            {
                ++depth;
                ++pos;
                continue;
            }
            if (ch == ')' && depth > 0)
            {
                --depth;
                ++pos;
                continue;
            }

            bool const isEntryComma = ch == ',' && depth == 0;
            bool const isListEnd = ch == ')' && depth == 0;
            if (isEntryComma || isListEnd)
            {
                if (size_t const afterName = MatchColumnEntryName(createSql, entryStart, pos, columnName);
                    afterName != std::string::npos)
                    return { afterName, pos };
                if (isListEnd)
                    break;
                entryStart = pos + 1;
            }
            ++pos;
        }
        return { std::string::npos, std::string::npos };
    }

    /// Rewrite a single column's type and nullability inside a stored `CREATE TABLE` statement,
    /// preserving the column's other inline constraints (PRIMARY KEY, UNIQUE, DEFAULT, …) and
    /// every other column verbatim. Backs the SQLite `AlterColumn` table rebuild.
    ///
    /// @param createSql The stored `CREATE TABLE` text (temp-table-named by the rebuild helper).
    /// @param columnName The column to alter.
    /// @param newType The new column type (already formatted, e.g. `TEXT` or `VARCHAR(200)`).
    /// @param notNull Whether the column should become `NOT NULL`.
    /// @return The transformed `CREATE TABLE` text.
    [[nodiscard]] std::string RewriteSqliteColumnDefinition(std::string createSql,
                                                            std::string_view columnName,
                                                            std::string_view newType,
                                                            bool notNull)
    {
        // Locate the target column's definition as a top-level entry of the column list, rather than by
        // a naive substring search for `"name"`. The column list is the first top-level `(...)` group;
        // its entries are separated by top-level commas, and a column definition is an entry whose first
        // token is the quoted column name. Walking the structure (skipping nested parens, string
        // literals, and quoted identifiers) keeps the match off a `CHECK("name" > 0)` expression, a
        // `REFERENCES "T"("name")` clause, a `DEFAULT '... "name" ...'` literal, or any other column
        // that merely mentions the name.
        //
        // We deliberately rewrite the stored DDL as text rather than reconstruct the column from PRAGMA
        // metadata: PRAGMA cannot faithfully round-trip CHECK / COLLATE / generated-column / inline-FK
        // clauses, all of which this text surgery preserves verbatim.
        size_t const listOpen = FindColumnListOpen(createSql);
        if (listOpen == std::string::npos)
            throw std::runtime_error(
                std::format(R"(SQLite rebuild: no column list found in CREATE TABLE for "{}")", columnName));

        // `afterName` sits just past the target column's quoted name; `defEnd` at the entry's
        // terminating comma or the list's ')'.
        auto const [afterName, defEnd] = LocateColumnDefinition(createSql, listOpen, columnName);
        if (afterName == std::string::npos)
            throw std::runtime_error(std::format(R"(SQLite rebuild: column "{}" not found in CREATE TABLE)", columnName));

        // Definition body after the column name: `<type> [type-params] <constraints...>`. The type token
        // is the first whitespace-delimited token, with any `(...)` parameter group attached (NextSqlToken
        // treats the group as opaque); the remainder is the surviving constraints.
        auto const body = std::string_view { createSql }.substr(afterName, defEnd - afterName);
        auto const [typeStart, typeEnd] = NextSqlToken(body, 0);
        std::string const rest =
            typeStart < typeEnd ? StripNullabilityKeywords(body.substr(typeEnd), notNull) : std::string {};

        // SQLite only allows `AUTOINCREMENT` on an `INTEGER PRIMARY KEY` column. Changing such a column
        // to any other type would emit DDL SQLite rejects mid-rebuild, so fail early with a clear,
        // actionable message. The check is token-based so it is not fooled by the text `AUTOINCREMENT`
        // appearing inside a DEFAULT literal or an identifier.
        if (!IEqualsAscii(newType, "INTEGER") && ContainsTokenAscii(rest, "AUTOINCREMENT"))
            throw std::runtime_error(std::format(R"(SQLite rebuild: cannot change AUTOINCREMENT column "{}" to type "{}" )"
                                                 R"((AUTOINCREMENT requires INTEGER PRIMARY KEY))",
                                                 columnName,
                                                 newType));

        // Reassemble ` <newType> [NOT NULL] <surviving constraints>` in place of the old definition
        // body. `afterName` sits at the closing quote of the name, so a single leading space is included.
        std::string rebuilt = " ";
        rebuilt += newType;
        if (notNull)
            rebuilt += " NOT NULL";
        if (!rest.empty())
        {
            rebuilt += ' ';
            rebuilt += rest;
        }

        createSql.replace(afterName, defEnd - afterName, rebuilt);
        return createSql;
    }

    /// Rebuild a SQLite table to change a column's type and/or nullability.
    ///
    /// SQLite has no `ALTER TABLE … ALTER COLUMN`, so the executor recreates the table from its
    /// stored `CREATE TABLE` definition with the one column's type/nullability rewritten, then
    /// copies the data across. Other columns, constraints, indexes, and triggers are preserved
    /// (indexes/triggers via @ref RebuildSqliteTable's capture-and-recreate step).
    void SqliteRebuildAlterColumn(SqlConnection& connection, SqliteGuard const& guard)
    {
        RebuildSqliteTable(connection, guard.tableName, [&](std::string createSql) {
            return RewriteSqliteColumnDefinition(std::move(createSql), guard.columnName, guard.columnType, guard.notNull);
        });
    }

    /// @brief RAII helper that disables SQLite foreign-key enforcement for the duration of a migration,
    /// then restores it. A table rebuild's `DROP TABLE` would otherwise fire cascade actions or leave
    /// another table's foreign key dangling when enforcement is on.
    ///
    /// SQLite only honours `PRAGMA foreign_keys` *outside* a transaction, and migrations run inside one,
    /// so the toggle must bracket the transaction: construct this before the `SqlTransaction` and let it
    /// outlive it (locals are destroyed in reverse order). It is a no-op on non-SQLite backends and when
    /// enforcement is already off (the SQLite default), so the common case is untouched.
    class SqliteForeignKeysGuard
    {
      public:
        /// @param connection The connection whose foreign-key enforcement to toggle.
        explicit SqliteForeignKeysGuard(SqlConnection& connection):
            _connection { connection }
        {
            if (!connection.RequiresTableRebuildForSchemaChange())
                return;
            bool enabled = false;
            {
                // Read the current value in its own scope so the result cursor is released before the
                // `SET` below: reusing a statement while its cursor is still open is an invalid-cursor-
                // state error on the SQLite ODBC driver.
                auto stmt = SqlStatement { connection };
                auto cursor = stmt.ExecuteDirect("PRAGMA foreign_keys");
                enabled = cursor.FetchRow() && cursor.GetColumn<int64_t>(1) != 0;
            }
            if (enabled)
            {
                auto stmt = SqlStatement { connection };
                (void) stmt.ExecuteDirect("PRAGMA foreign_keys = OFF");
                _restore = true;
            }
        }

        ~SqliteForeignKeysGuard()
        {
            if (!_restore)
                return;
            // Destructors must not propagate. Route a failed restore to the active SqlLogger so a
            // silently-left-disabled foreign-key enforcement doesn't become an invisible regression.
            try
            {
                auto stmt = SqlStatement { _connection };
                (void) stmt.ExecuteDirect("PRAGMA foreign_keys = ON");
            }
            catch (...)
            {
                try
                {
                    SqlLogger::GetLogger().OnWarning("SqliteForeignKeysGuard: failed to restore PRAGMA foreign_keys = ON");
                }
                // NOLINTNEXTLINE(bugprone-empty-catch) — destructor must never throw.
                catch (...)
                {
                }
            }
        }

        SqliteForeignKeysGuard(SqliteForeignKeysGuard const&) = delete;
        SqliteForeignKeysGuard& operator=(SqliteForeignKeysGuard const&) = delete;
        SqliteForeignKeysGuard(SqliteForeignKeysGuard&&) = delete;
        SqliteForeignKeysGuard& operator=(SqliteForeignKeysGuard&&) = delete;

      private:
        SqlConnection& _connection;
        bool _restore = false;
    };

    /// Execute a SQL script that may be prefixed with a SQLite runtime-guard sentinel.
    ///
    /// If the script carries a guard, perform the presence check first and skip the DDL
    /// body when the guard condition is already satisfied (or unsatisfied for DROP).
    /// Otherwise execute the script directly.
    void ExecuteScriptRespectingSqliteGuards(SqlStatement& stmt, SqlConnection& connection, std::string_view script)
    {
        auto const parsed = TryParseSqliteGuard(script);
        if (!parsed || !connection.RequiresTableRebuildForSchemaChange())
        {
            (void) stmt.ExecuteDirect(script);
            return;
        }

        auto const& guard = parsed->first;
        auto const bodyStart = parsed->second;
        auto const body = script.substr(bodyStart);

        switch (guard.kind)
        {
            case SqliteGuard::Kind::AddColumnIfNotExists:
                if (!SqliteColumnExists(connection, guard.tableName, guard.columnName))
                    (void) stmt.ExecuteDirect(body);
                return;
            case SqliteGuard::Kind::DropColumnIfExists:
                if (SqliteColumnExists(connection, guard.tableName, guard.columnName))
                    (void) stmt.ExecuteDirect(body);
                return;
            case SqliteGuard::Kind::AddForeignKey:
                SqliteRebuildAddForeignKey(connection, guard);
                return;
            case SqliteGuard::Kind::DropForeignKey:
                SqliteRebuildDropForeignKey(connection, guard);
                return;
            case SqliteGuard::Kind::AddCompositeForeignKey:
                SqliteRebuildAddCompositeForeignKey(connection, guard);
                return;
            case SqliteGuard::Kind::AlterColumn:
                SqliteRebuildAlterColumn(connection, guard);
                return;
        }
    }
} // namespace

namespace
{
    /// @brief Maps the SQL-Server / ANSI-style data type names emitted by
    /// `INFORMATION_SCHEMA.COLUMNS` (`DATA_TYPE` column) to a `ColumnWidth`. Returns
    /// `value=0` for non-character types so the caller can skip them.
    ///
    /// `varchar`/`char` are byte-counted (server budget is in bytes), `nvarchar`/`nchar`
    /// are character-counted. `INFORMATION_SCHEMA.CHARACTER_MAXIMUM_LENGTH` already
    /// reports chars for the N-prefixed types and bytes for the others, so the value
    /// passes through unchanged — only the unit varies.
    [[nodiscard]] MigrationRenderContext::ColumnWidth CharacterWidthFromDataType(std::string_view dataType,
                                                                                 std::size_t maxLength) noexcept
    {
        using Unit = MigrationRenderContext::WidthUnit;
        if (dataType == "nvarchar" || dataType == "nchar")
            return { .value = maxLength, .unit = Unit::Characters };
        if (dataType == "varchar" || dataType == "char")
            return { .value = maxLength, .unit = Unit::Bytes };
        return { .value = 0, .unit = Unit::Characters };
    }

    /// @brief Builds a lazy `widthLookup` callback that resolves missing widths via
    /// `INFORMATION_SCHEMA.COLUMNS` against the supplied connection. The callback is
    /// safe to invoke many times — every (schema, table) is queried at most once per
    /// run thanks to `MigrationRenderContext::lookupAttempted`.
    ///
    /// Errors (e.g. INFORMATION_SCHEMA missing on SQLite, table not yet created) are
    /// swallowed: the render path already treats a missing width as "don't truncate",
    /// which is the same behaviour we want when the lookup fails for any reason.
    auto MakeWidthLookup(SqlConnection& connection)
    {
        return [&connection](MigrationRenderContext& ctx, std::string_view schema, std::string_view table) {
            auto const sql =
                schema.empty()
                    ? std::format("SELECT COLUMN_NAME, DATA_TYPE, CHARACTER_MAXIMUM_LENGTH "
                                  "FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_NAME = '{}'",
                                  table)
                    : std::format("SELECT COLUMN_NAME, DATA_TYPE, CHARACTER_MAXIMUM_LENGTH "
                                  "FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = '{}' AND TABLE_NAME = '{}'",
                                  schema,
                                  table);

            auto stmt = SqlStatement { connection };
            try
            {
                auto cursor = stmt.ExecuteDirect(sql);
                while (cursor.FetchRow())
                {
                    auto const columnName = cursor.GetColumn<std::string>(1);
                    auto const dataType = cursor.GetColumn<std::string>(2);
                    // CHARACTER_MAXIMUM_LENGTH is NULL for non-character columns —
                    // those rows are skipped silently.
                    auto const maxLengthOpt = cursor.GetNullableColumn<long long>(3);
                    if (!maxLengthOpt.has_value() || *maxLengthOpt <= 0)
                        continue;
                    auto const width = CharacterWidthFromDataType(dataType, static_cast<std::size_t>(*maxLengthOpt));
                    if (width.value == 0)
                        continue;
                    ctx.columnWidths[MigrationRenderContext::ColumnKey {
                        .schema = std::string(schema), .table = std::string(table), .column = columnName }] = width;
                }
            }
            catch (SqlException const&) // NOLINT(bugprone-empty-catch) — see function comment
            {
                // Suppressed by design — see function-level comment.
            }
        };
    }
} // namespace

MigrationRenderContext MigrationManager::MakeRenderContext()
{
    return MigrationRenderContext {};
}

void MigrationManager::ApplySingleMigration(MigrationBase const& migration)
{
    auto ctx = MakeRenderContext();
    ctx.widthLookup = MakeWidthLookup(GetDataMapper().Connection());
    ApplySingleMigration(migration, ctx);
}

void MigrationManager::ApplySingleMigration(MigrationBase const& migration, MigrationRenderContext& context)
{
    // We are about to write a real `schema_migrations` row, so any overlay
    // entries contributed by plugin post-init hooks have to be materialised
    // first — both so dependency checks below see the full history and so the
    // table's row order matches the actual applied order. `PersistVirtualAppliedMigrations`
    // is a no-op when the overlay is empty.
    PersistVirtualAppliedMigrations();

    // Re-derive compat knobs for this specific migration. The column-width cache in
    // `context` persists across migrations (on purpose — CREATE TABLE in migration N
    // populates widths an INSERT in migration N+k consults); the per-migration knobs
    // do not.
    auto const flags = CompatFlagsFor(migration);
    context.lupTruncate = flags.contains(std::string(CompatFlagLupTruncateName));
    context.activeMigrationTimestamp = migration.GetTimestamp().value;
    context.activeMigrationTitle = std::string { migration.GetTitle() };
    // Check declared dependencies: every dependency must already be applied.
    if (auto const deps = migration.GetDependencies(); !deps.empty())
    {
        auto const applied = GetAppliedMigrationIds();
        for (auto const& dep: deps)
        {
            if (!std::ranges::contains(applied, dep))
            {
                throw std::runtime_error(
                    std::format("Migration '{}' (timestamp {}) cannot be applied: dependency {} is not applied.",
                                migration.GetTitle(),
                                migration.GetTimestamp().value,
                                dep.value));
            }
        }
    }

    auto& dm = GetDataMapper();
    // Disable SQLite FK enforcement around the transaction so a table rebuild's DROP TABLE cannot
    // cascade or leave a referencing table's FK dangling (no-op off SQLite / when already disabled).
    auto foreignKeysGuard = SqliteForeignKeysGuard { dm.Connection() };
    auto transaction = SqlTransaction { dm.Connection(), SqlTransactionMode::ROLLBACK };

    SqlMigrationQueryBuilder migrationBuilder = dm.Connection().Migration();
    migration.Up(migrationBuilder);

    SqlMigrationPlan const plan = std::move(migrationBuilder).GetPlan();

    auto stmt = SqlStatement { dm.Connection() };
    size_t stepIndex = 0;

    auto const startTime = std::chrono::steady_clock::now();

    for (SqlMigrationPlanElement const& step: plan.steps)
    {
        auto const sqlScripts = ToSql(dm.Connection().QueryFormatter(), step, context);
        for (auto const& sqlScript: sqlScripts)
        {
            try
            {
                ExecuteScriptRespectingSqliteGuards(stmt, dm.Connection(), sqlScript);
            }
            catch (SqlException const& ex)
            {
                throw MigrationException(MigrationException::Operation::Apply,
                                         migration.GetTimestamp(),
                                         std::string { migration.GetTitle() },
                                         stepIndex,
                                         sqlScript,
                                         ex.info());
            }
        }
        ++stepIndex;
    }

    auto const elapsedMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count());

    auto const checksum = migration.ComputeChecksum(dm.Connection().QueryFormatter());
    dm.CreateExplicit(SchemaMigration { .version = migration.GetTimestamp().value,
                                        .checksum = checksum,
                                        .applied_at = SqlDateTime::Now(),
                                        .author = MakeOptionalSqlString128(migration.GetAuthor()),
                                        .description = MakeOptionalSqlString1024(migration.GetDescription()),
                                        .execution_duration_ms = elapsedMs });
    transaction.Commit();
}

void MigrationManager::RevertSingleMigration(MigrationBase const& migration)
{
    // Revert deletes a `schema_migrations` row, so the overlay (if any) needs
    // to be drained first. A virtual-applied entry has no on-disk row to
    // remove; persisting it before the revert lets `DELETE FROM schema_migrations`
    // operate uniformly on the materialised history without a separate
    // overlay-aware code path.
    PersistVirtualAppliedMigrations();

    // Check if Down() is implemented before attempting revert
    if (!migration.HasDownImplementation())
    {
        throw std::runtime_error(std::format("Migration '{}' (timestamp {}) cannot be reverted: Down() is not implemented.",
                                             migration.GetTitle(),
                                             migration.GetTimestamp().value));
    }

    auto& dm = GetDataMapper();
    // Disable SQLite FK enforcement around the transaction so a table rebuild's DROP TABLE cannot
    // cascade or leave a referencing table's FK dangling (no-op off SQLite / when already disabled).
    auto foreignKeysGuard = SqliteForeignKeysGuard { dm.Connection() };
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
                ExecuteScriptRespectingSqliteGuards(stmt, dm.Connection(), sqlScript);
            }
            catch (SqlException const& ex)
            {
                throw MigrationException(MigrationException::Operation::Revert,
                                         migration.GetTimestamp(),
                                         std::string { migration.GetTitle() },
                                         stepIndex,
                                         sqlScript,
                                         ex.info());
            }
        }
        ++stepIndex;
    }

    dm.Query<SchemaMigration>().Where("version", "=", migration.GetTimestamp().value).Delete();
    transaction.Commit();
}

namespace
{
    /// Returns those of `pending` whose timestamp is `<= targetInclusive`, preserving the
    /// caller's order. If any kept migration depends on an excluded (`> targetInclusive`)
    /// pending migration that is also not in `applied`, throws — applying the kept set
    /// would violate the dependency contract, and `ValidateDependencies` already passed
    /// against the unfiltered set so we have to surface this here.
    std::list<MigrationBase const*> FilterPendingUpTo(std::list<MigrationBase const*> const& pending,
                                                      MigrationTimestamp targetInclusive,
                                                      std::vector<MigrationTimestamp> const& applied)
    {
        std::unordered_set<uint64_t> appliedSet;
        appliedSet.reserve(applied.size());
        for (auto const& a: applied)
            appliedSet.insert(a.value);

        std::unordered_set<uint64_t> includedSet;
        includedSet.reserve(pending.size());
        for (auto const* m: pending)
            if (m->GetTimestamp().value <= targetInclusive.value)
                includedSet.insert(m->GetTimestamp().value);

        for (auto const* m: pending)
        {
            if (m->GetTimestamp().value > targetInclusive.value)
                continue;
            for (auto const& dep: m->GetDependencies())
            {
                if (appliedSet.contains(dep.value) || includedSet.contains(dep.value))
                    continue;
                throw std::runtime_error(
                    std::format("Migration {} (\"{}\") depends on migration {}, which is past the requested "
                                "target {}. Migrating to this point would violate dependency ordering.",
                                m->GetTimestamp().value,
                                m->GetTitle(),
                                dep.value,
                                targetInclusive.value));
            }
        }

        std::list<MigrationBase const*> result;
        for (auto const* m: pending)
            if (m->GetTimestamp().value <= targetInclusive.value)
                result.push_back(m);
        return result;
    }
} // namespace

size_t MigrationManager::ApplyPendingMigrations(ExecuteCallback const& feedbackCallback)
{
    ValidateDependencies();
    auto const pendingMigrations = GetPending();

    // Shared render context so column-width state accumulates across the whole run —
    // a CREATE TABLE in migration N populates widths an INSERT in migration N+k will
    // consult when compat flags are active. The width-lookup fallback covers the case
    // where the destination table was already present from a previous run.
    auto context = MakeRenderContext();
    context.widthLookup = MakeWidthLookup(GetDataMapper().Connection());

#if !defined(__cpp_lib_ranges_enumerate)
    int index { -1 };
    for (auto const& migration: pendingMigrations)
    {
        ++index;
#else
    for (auto&& [index, migration]: pendingMigrations | std::views::enumerate)
    {
#endif
        if (feedbackCallback)
            feedbackCallback(*migration, static_cast<size_t>(index), _migrations.size());
        ApplySingleMigration(*migration, context);
    }

    return pendingMigrations.size();
}

size_t MigrationManager::ApplyPendingMigrationsUpTo(MigrationTimestamp targetInclusive,
                                                    ExecuteCallback const& feedbackCallback)
{
    ValidateDependencies();
    auto const pendingMigrations = FilterPendingUpTo(GetPending(), targetInclusive, GetAppliedMigrationIds());

    auto context = MakeRenderContext();
    context.widthLookup = MakeWidthLookup(GetDataMapper().Connection());

#if !defined(__cpp_lib_ranges_enumerate)
    int index { -1 };
    for (auto const& migration: pendingMigrations)
    {
        ++index;
#else
    for (auto&& [index, migration]: pendingMigrations | std::views::enumerate)
    {
#endif
        if (feedbackCallback)
            feedbackCallback(*migration, static_cast<size_t>(index), pendingMigrations.size());
        ApplySingleMigration(*migration, context);
    }

    return pendingMigrations.size();
}

SqlTransaction MigrationManager::Transaction()
{
    return SqlTransaction { GetDataMapper().Connection() };
}

std::vector<std::string> MigrationManager::PreviewMigration(MigrationBase const& migration) const
{
    auto ctx = MakeRenderContext();
    ctx.widthLookup = MakeWidthLookup(GetDataMapper().Connection());
    return PreviewMigrationWithContext(migration, ctx);
}

std::vector<std::string> MigrationManager::PreviewMigrationWithContext(MigrationBase const& migration,
                                                                       MigrationRenderContext& context) const
{
    auto const flags = CompatFlagsFor(migration);
    context.lupTruncate = flags.contains(std::string(CompatFlagLupTruncateName));
    context.activeMigrationTimestamp = migration.GetTimestamp().value;
    context.activeMigrationTitle = std::string { migration.GetTitle() };

    auto& dm = GetDataMapper();
    SqlMigrationQueryBuilder migrationBuilder = dm.Connection().Migration();
    migration.Up(migrationBuilder);

    SqlMigrationPlan const plan = std::move(migrationBuilder).GetPlan();
    std::vector<std::string> out;
    for (auto const& step: plan.steps)
    {
        auto sql = ToSql(plan.formatter, step, context);
        out.insert(out.end(), sql.begin(), sql.end());
    }
    return out;
}

std::vector<std::string> MigrationManager::PreviewPendingMigrations(ExecuteCallback const& feedbackCallback) const
{
    auto const pendingMigrations = GetPending();
    std::vector<std::string> allStatements;

    // See `ApplyPendingMigrations` — shared context so earlier CREATE TABLE widths are
    // visible to later INSERT/UPDATE preview rendering.
    auto context = MakeRenderContext();
    context.widthLookup = MakeWidthLookup(GetDataMapper().Connection());

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

        auto statements = PreviewMigrationWithContext(*migration, context);
        allStatements.insert(allStatements.end(), statements.begin(), statements.end());
    }

    return allStatements;
}

std::vector<std::string> MigrationManager::PreviewPendingMigrationsUpTo(MigrationTimestamp targetInclusive,
                                                                        ExecuteCallback const& feedbackCallback) const
{
    auto const pendingMigrations = FilterPendingUpTo(GetPending(), targetInclusive, GetAppliedMigrationIds());
    std::vector<std::string> allStatements;

    auto context = MakeRenderContext();
    context.widthLookup = MakeWidthLookup(GetDataMapper().Connection());

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

        auto statements = PreviewMigrationWithContext(*migration, context);
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

MigrationManager::RewriteChecksumsResult MigrationManager::RewriteChecksums(bool dryRun)
{
    RewriteChecksumsResult result;
    result.wasDryRun = dryRun;

    auto& dm = GetDataMapper();
    auto records = dm.Query<SchemaMigration>().OrderBy("version", SqlResultOrdering::ASCENDING).All();

    auto transaction = SqlTransaction { dm.Connection(), SqlTransactionMode::ROLLBACK };

    for (auto& record: records)
    {
        auto const timestamp = MigrationTimestamp { record.version.Value() };
        auto const* migration = GetMigration(timestamp);
        if (!migration)
        {
            result.unregisteredTimestamps.push_back(timestamp);
            continue;
        }

        auto const& storedOpt = record.checksum.Value();
        std::string const stored = storedOpt.has_value() ? std::string(storedOpt->str()) : std::string {};
        auto const computed = migration->ComputeChecksum(dm.Connection().QueryFormatter());
        if (stored == computed)
            continue;

        result.entries.push_back(ChecksumRewriteEntry {
            .timestamp = timestamp, .title = migration->GetTitle(), .oldChecksum = stored, .newChecksum = computed });

        if (!dryRun)
        {
            record.checksum = computed;
            dm.Update(record);
        }
    }

    if (!dryRun)
        transaction.Commit();

    return result;
}

std::vector<ChecksumVerificationResult> MigrationManager::VerifyChecksums() const
{
    std::vector<ChecksumVerificationResult> results;
    auto& dm = GetDataMapper();

    // Get all applied migrations with their checksums. The table may not yet
    // exist — that's the legitimate "fresh DB / overlay-only" state in the
    // post-overlay design, so swallow the missing-table error and return an
    // empty result. Virtual-applied overlay entries are deliberately not
    // checked here: they carry no stored checksum and behave like the
    // pre-checksum legacy rows (storedChecksum empty ⇒ treated as match).
    auto appliedMigrations = std::vector<SchemaMigration> {};
    try
    {
        appliedMigrations = dm.Query<SchemaMigration>().OrderBy("version", SqlResultOrdering::ASCENDING).All();
    }
    catch (SqlException const&)
    {
        return results;
    }

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

void MigrationManager::MarkMigrationAsApplied(MigrationBase const& migration)
{
    // Drain the overlay before writing this row. If the caller is bulk-marking
    // a sequence of migrations on top of a virtual-applied overlay, this is
    // also what guarantees the overlay timestamps land in the table ahead of
    // the new row — preserving the natural `version ASC` ordering on disk.
    PersistVirtualAppliedMigrations();

    auto& dm = GetDataMapper();

    // Check if already applied
    auto const appliedIds = GetAppliedMigrationIds();
    if (std::ranges::contains(appliedIds, migration.GetTimestamp()))
    {
        throw std::runtime_error(std::format("Migration '{}' (timestamp {}) is already marked as applied.",
                                             migration.GetTitle(),
                                             migration.GetTimestamp().value));
    }

    // Compute checksum using the current formatter
    auto const checksum = migration.ComputeChecksum(dm.Connection().QueryFormatter());

    // Insert into schema_migrations without executing Up(); duration stays null.
    dm.CreateExplicit(SchemaMigration {
        .version = migration.GetTimestamp().value,
        .checksum = checksum,
        .applied_at = SqlDateTime::Now(),
        .author = MakeOptionalSqlString128(migration.GetAuthor()),
        .description = MakeOptionalSqlString1024(migration.GetDescription()),
        .execution_duration_ms = std::nullopt,
    });
}

RevertResult MigrationManager::RevertToMigration(MigrationTimestamp target, ExecuteCallback const& feedbackCallback)
{
    RevertResult result;

    // Get all applied migrations
    auto appliedIds = GetAppliedMigrationIds();

    // Filter to only migrations > target and sort in reverse order (newest first)
    std::vector<MigrationTimestamp> toRevert;
    for (auto const& id: appliedIds)
    {
        if (id > target)
            toRevert.push_back(id);
    }

    // Sort in descending order (revert newest first)
    std::ranges::sort(toRevert, std::greater<> {});

    if (toRevert.empty())
    {
        return result; // Nothing to revert
    }

    // Revert each migration
    size_t current = 0;
    for (auto const& timestamp: toRevert)
    {
        auto const* migration = GetMigration(timestamp);

        if (!migration)
        {
            result.failedAt = timestamp;
            result.errorMessage = std::format(
                "Migration with timestamp {} is applied but not found in registered migrations.", timestamp.value);
            return result;
        }

        if (feedbackCallback)
        {
            feedbackCallback(*migration, current, toRevert.size());
        }

        try
        {
            RevertSingleMigration(*migration);
            result.revertedTimestamps.push_back(timestamp);
        }
        catch (MigrationException const& ex)
        {
            // Preserve the same timestamp mapping callers already rely on,
            // but pull the *structured* diagnostic fields out of the
            // exception — so the CLI and GUI can render a breakdown rather
            // than parsing a single formatted message.
            result.failedAt = timestamp;
            result.failedTitle = ex.GetMigrationTitle();
            result.failedStepIndex = ex.GetStepIndex();
            result.failedSql = ex.GetFailedSql();
            result.sqlState = ex.info().sqlState;
            result.nativeErrorCode = ex.info().nativeErrorCode;
            // errorMessage keeps the raw driver message (no migration prefix),
            // so CLI/GUI callers can build their own layout around it without
            // stripping the composed summary.
            result.errorMessage = ex.GetDriverMessage();
            return result;
        }
        catch (std::exception const& ex)
        {
            result.failedAt = timestamp;
            result.failedTitle = std::string { migration->GetTitle() };
            result.errorMessage = ex.what();
            return result;
        }

        ++current;
    }

    return result;
}

namespace
{
    /// @brief Builds the (catalog, schema, table) tuple used as a key in the fold result.
    SqlSchema::FullyQualifiedTableName MakeFqtn(std::string_view schema, std::string_view table)
    {
        return SqlSchema::FullyQualifiedTableName {
            .catalog = {},
            .schema = std::string(schema),
            .table = std::string(table),
        };
    }

    /// @brief Drops the FK whose local column matches `columnName` from `state`'s
    /// composite-FK list. Single-column-FK declarations carried inline on a column
    /// declaration (`SqlColumnDeclaration::foreignKey`) are NOT touched here — the
    /// caller is responsible for clearing those when the column itself is dropped.
    void DropFkByColumn(MigrationManager::PlanFoldingResult::TableState& state, std::string_view columnName)
    {
        std::erase_if(state.compositeForeignKeys, [&](SqlCompositeForeignKeyConstraint const& fk) {
            return fk.columns.size() == 1 && fk.columns.front() == columnName;
        });
    }

    /// @brief Removes any column declaration whose name matches `columnName` and the
    /// FK constraints that reference it. Returns true if a column was actually removed.
    bool RemoveColumn(MigrationManager::PlanFoldingResult::TableState& state, std::string_view columnName)
    {
        auto const before = state.columns.size();
        std::erase_if(state.columns, [&](SqlColumnDeclaration const& c) { return c.name == columnName; });
        DropFkByColumn(state, columnName);
        return state.columns.size() != before;
    }

    /// @brief Renames a column in `state.columns`, plus any FK declaration referencing
    /// the old name. Inline FKs on the renamed column are preserved.
    void RenameColumnInState(MigrationManager::PlanFoldingResult::TableState& state,
                             std::string_view oldName,
                             std::string_view newName)
    {
        for (auto& c: state.columns)
            if (c.name == oldName)
                c.name = std::string(newName);
        for (auto& fk: state.compositeForeignKeys)
            for (auto& col: fk.columns)
                if (col == oldName)
                    col = std::string(newName);
    }

    /// @brief Re-keys a table in `tables` and `creationOrder` from `oldName` → `newName`.
    /// Updates inbound FK references in every other table so they continue to point at
    /// the renamed table. Indexes hosted on the renamed table are also rewritten.
    void RenameTableInResult(MigrationManager::PlanFoldingResult& result,
                             std::string_view schema,
                             std::string_view oldName,
                             std::string_view newName)
    {
        auto const oldKey = MakeFqtn(schema, oldName);
        auto const newKey = MakeFqtn(schema, newName);
        auto it = result.tables.find(oldKey);
        if (it == result.tables.end())
            return;
        auto state = std::move(it->second);
        result.tables.erase(it);
        result.tables.emplace(newKey, std::move(state));

        for (auto& entry: result.creationOrder)
            if (entry == oldKey)
                entry = newKey;

        // Rewrite FK references in every table that points at oldName (composite FKs).
        for (auto& [_, otherState]: result.tables)
        {
            for (auto& fk: otherState.compositeForeignKeys)
                if (fk.referencedTableName == oldName)
                    fk.referencedTableName = std::string(newName);
            for (auto& col: otherState.columns)
                if (col.foreignKey && col.foreignKey->tableName == oldName)
                    col.foreignKey->tableName = std::string(newName);
        }

        // Rewrite indexes hosted on the renamed table.
        for (auto& idx: result.indexes)
            if (idx.schemaName == schema && idx.tableName == oldName)
                idx.tableName = std::string(newName);
    }

    /// @brief Drops a table from the result and any side-effect references (indexes,
    /// inbound FKs from other tables, queued data steps targeting the dropped table).
    void DropTableFromResult(MigrationManager::PlanFoldingResult& result,
                             std::string_view schema,
                             std::string_view tableName)
    {
        auto const key = MakeFqtn(schema, tableName);
        result.tables.erase(key);
        std::erase(result.creationOrder, key);

        std::erase_if(result.indexes,
                      [&](SqlCreateIndexPlan const& idx) { return idx.schemaName == schema && idx.tableName == tableName; });

        // Drop inbound FKs from other tables — and inline FK declarations.
        for (auto& [_, state]: result.tables)
        {
            std::erase_if(state.compositeForeignKeys,
                          [&](SqlCompositeForeignKeyConstraint const& fk) { return fk.referencedTableName == tableName; });
            for (auto& c: state.columns)
                if (c.foreignKey && c.foreignKey->tableName == tableName)
                    c.foreignKey.reset();
        }

        // Drop queued data steps targeting the dropped table.
        std::erase_if(result.dataSteps, [&](MigrationManager::PlanFoldingResult::DataStep const& step) {
            return std::visit(
                ::Lightweight::detail::overloaded {
                    [&](SqlInsertDataPlan const& s) { return s.schemaName == schema && s.tableName == tableName; },
                    [&](SqlUpdateDataPlan const& s) { return s.schemaName == schema && s.tableName == tableName; },
                    [&](SqlDeleteDataPlan const& s) { return s.schemaName == schema && s.tableName == tableName; },
                    [](auto const&) { return false; },
                },
                step.element);
        });
    }

    /// @brief Apply one ALTER TABLE command to the fold's `TableState`.
    void ApplyAlterCommand(MigrationManager::PlanFoldingResult::TableState& state,
                           MigrationManager::PlanFoldingResult& result,
                           std::string_view schema,
                           std::string_view tableName,
                           SqlAlterTableCommand const& cmd)
    {
        std::visit(::Lightweight::detail::overloaded {
                       [&](SqlAlterTableCommands::RenameTable const& c) {
                           RenameTableInResult(result, schema, tableName, c.newTableName);
                       },
                       [&](SqlAlterTableCommands::AddColumn const& c) {
                           state.columns.push_back(SqlColumnDeclaration {
                               .name = c.columnName,
                               .type = c.columnType,
                               .required = c.nullable == SqlNullable::NotNull,
                           });
                       },
                       [&](SqlAlterTableCommands::AddColumnIfNotExists const& c) {
                           auto const exists = std::ranges::any_of(
                               state.columns, [&](SqlColumnDeclaration const& d) { return d.name == c.columnName; });
                           if (!exists)
                               state.columns.push_back(SqlColumnDeclaration {
                                   .name = c.columnName,
                                   .type = c.columnType,
                                   .required = c.nullable == SqlNullable::NotNull,
                               });
                       },
                       [&](SqlAlterTableCommands::AlterColumn const& c) {
                           for (auto& d: state.columns)
                           {
                               if (d.name == c.columnName)
                               {
                                   d.type = c.columnType;
                                   d.required = c.nullable == SqlNullable::NotNull;
                               }
                           }
                       },
                       [&](SqlAlterTableCommands::RenameColumn const& c) {
                           RenameColumnInState(state, c.oldColumnName, c.newColumnName);
                       },
                       [&](SqlAlterTableCommands::DropColumn const& c) { (void) RemoveColumn(state, c.columnName); },
                       [&](SqlAlterTableCommands::DropColumnIfExists const& c) { (void) RemoveColumn(state, c.columnName); },
                       [&](SqlAlterTableCommands::AddIndex const& c) {
                           result.indexes.push_back(SqlCreateIndexPlan {
                               .schemaName = std::string(schema),
                               .indexName = std::format("idx_{}_{}", tableName, c.columnName),
                               .tableName = std::string(tableName),
                               .columns = { std::string(c.columnName) },
                               .unique = c.unique,
                           });
                       },
                       [&](SqlAlterTableCommands::DropIndex const& c) {
                           std::erase_if(result.indexes, [&](SqlCreateIndexPlan const& i) {
                               return i.schemaName == schema && i.tableName == tableName && i.columns.size() == 1
                                      && i.columns.front() == c.columnName;
                           });
                       },
                       [&](SqlAlterTableCommands::DropIndexIfExists const& c) {
                           std::erase_if(result.indexes, [&](SqlCreateIndexPlan const& i) {
                               return i.schemaName == schema && i.tableName == tableName && i.columns.size() == 1
                                      && i.columns.front() == c.columnName;
                           });
                       },
                       [&](SqlAlterTableCommands::AddForeignKey const& c) {
                           // Promote single-column FK to composite list — same logical
                           // shape, simpler to fold across renames/drops.
                           state.compositeForeignKeys.push_back(SqlCompositeForeignKeyConstraint {
                               .columns = { c.columnName },
                               .referencedTableName = c.referencedColumn.tableName,
                               .referencedColumns = { c.referencedColumn.columnName },
                           });
                       },
                       [&](SqlAlterTableCommands::AddCompositeForeignKey const& c) {
                           state.compositeForeignKeys.push_back(SqlCompositeForeignKeyConstraint {
                               .columns = c.columns,
                               .referencedTableName = c.referencedTableName,
                               .referencedColumns = c.referencedColumns,
                           });
                       },
                       [&](SqlAlterTableCommands::DropForeignKey const& c) { DropFkByColumn(state, c.columnName); },
                   },
                   cmd);
    }
} // namespace

MigrationManager::PlanFoldingResult MigrationManager::FoldRegisteredMigrations(
    SqlQueryFormatter const& formatter, std::optional<MigrationTimestamp> upToInclusive) const
{
    PlanFoldingResult result;

    // Walk migrations in timestamp order (already sorted by `AddMigration`).
    for (auto const* migration: _migrations)
    {
        if (upToInclusive.has_value() && migration->GetTimestamp() > *upToInclusive)
            break;

        result.foldedMigrations.emplace_back(migration->GetTimestamp(), std::string(migration->GetTitle()));

        SqlMigrationQueryBuilder builder { formatter };
        migration->Up(builder);
        SqlMigrationPlan plan = std::move(builder).GetPlan();

        for (SqlMigrationPlanElement const& step: plan.steps)
        {
            std::visit(::Lightweight::detail::overloaded {
                           [&](SqlCreateTablePlan const& s) {
                               auto const key = MakeFqtn(s.schemaName, s.tableName);
                               auto [it, inserted] = result.tables.try_emplace(key);
                               if (inserted)
                                   result.creationOrder.push_back(key);
                               it->second.columns = s.columns;
                               it->second.compositeForeignKeys = s.foreignKeys;
                               it->second.ifNotExists = s.ifNotExists;
                           },
                           [&](SqlAlterTablePlan const& s) {
                               auto const key = MakeFqtn(s.schemaName, s.tableName);
                               auto it = result.tables.find(key);
                               if (it == result.tables.end())
                                   return;
                               for (auto const& cmd: s.commands)
                                   ApplyAlterCommand(it->second, result, s.schemaName, s.tableName, cmd);
                           },
                           [&](SqlDropTablePlan const& s) { DropTableFromResult(result, s.schemaName, s.tableName); },
                           [&](SqlCreateIndexPlan const& s) { result.indexes.push_back(s); },
                           [&](SqlInsertDataPlan const& s) {
                               result.dataSteps.push_back(PlanFoldingResult::DataStep {
                                   .sourceTimestamp = migration->GetTimestamp(),
                                   .sourceTitle = std::string(migration->GetTitle()),
                                   .element = s,
                               });
                           },
                           [&](SqlUpdateDataPlan const& s) {
                               result.dataSteps.push_back(PlanFoldingResult::DataStep {
                                   .sourceTimestamp = migration->GetTimestamp(),
                                   .sourceTitle = std::string(migration->GetTitle()),
                                   .element = s,
                               });
                           },
                           [&](SqlDeleteDataPlan const& s) {
                               result.dataSteps.push_back(PlanFoldingResult::DataStep {
                                   .sourceTimestamp = migration->GetTimestamp(),
                                   .sourceTitle = std::string(migration->GetTitle()),
                                   .element = s,
                               });
                           },
                           [&](SqlRawSqlPlan const& s) {
                               result.dataSteps.push_back(PlanFoldingResult::DataStep {
                                   .sourceTimestamp = migration->GetTimestamp(),
                                   .sourceTitle = std::string(migration->GetTitle()),
                                   .element = s,
                               });
                           },
                       },
                       step);
        }
    }

    // Releases that fall within the fold range.
    auto const cutoff = upToInclusive.value_or(MigrationTimestamp { std::numeric_limits<uint64_t>::max() });
    for (auto const& release: _releases)
        if (release.highestTimestamp <= cutoff)
            result.releases.push_back(release);

    return result;
}

namespace
{
    /// @brief Returns true when `liveType` and `intendedType` form a valid Unicode-upgrade
    /// pair: live is byte-counted (`Char` / `Varchar`), intended is char-counted (`NChar`
    /// / `NVarchar`), and the declared `size` matches. Same-size matching is the
    /// conservative rule — it avoids accidentally widening a column whose declared size
    /// the migrations changed in tandem with the type.
    bool IsUnicodeUpgradeCandidate(SqlColumnTypeDefinition const& liveType, SqlColumnTypeDefinition const& intendedType)
    {
        if (auto const* lc = std::get_if<SqlColumnTypeDefinitions::Char>(&liveType))
            if (auto const* ic = std::get_if<SqlColumnTypeDefinitions::NChar>(&intendedType))
                return lc->size == ic->size;
        if (auto const* lv = std::get_if<SqlColumnTypeDefinitions::Varchar>(&liveType))
            if (auto const* iv = std::get_if<SqlColumnTypeDefinitions::NVarchar>(&intendedType))
                return lv->size == iv->size;
        return false;
    }
} // namespace

MigrationManager::HardResetResult MigrationManager::HardReset(bool dryRun)
{
    HardResetResult result;
    result.wasDryRun = dryRun;

    auto& dm = GetDataMapper();
    auto const& formatter = dm.Connection().QueryFormatter();
    auto const fold = FoldRegisteredMigrations(formatter);

    // Discover live tables. SchemaMigration handling is separate.
    auto stmt = SqlStatement { dm.Connection() };
    auto const liveTables = SqlSchema::ReadAllTables(stmt, std::string {}, std::string {});

    // Bookkeeping tables owned by the active advisory-lock handler
    // (`_lightweight_locks` on SQLite; empty on SQL Server / PostgreSQL
    // since they use server-native advisory locks). These belong to
    // Lightweight infrastructure, not user data, so they're dropped
    // alongside `schema_migrations` and never reported as preserved.
    auto const bookkeepingTableNames = formatter.AdvisoryLockOps().BookkeepingTableNames();
    std::set<std::string_view> const bookkeepingNamesSet { bookkeepingTableNames.begin(), bookkeepingTableNames.end() };

    std::set<std::string> intendedNames;
    for (auto const& key: fold.creationOrder)
        intendedNames.insert(key.table);

    std::set<std::string> liveNames;
    for (auto const& t: liveTables)
        liveNames.insert(t.name);

    // Walk in reverse creation order so dependent tables get dropped first.
    for (auto const& key: std::ranges::reverse_view(fold.creationOrder))
    {
        if (liveNames.contains(key.table))
            result.droppedTables.push_back(key);
        else
            result.absentTables.push_back(key);
    }

    // Live tables not declared by any migration → preserved (user-owned).
    // Comparison is by name only because the engine resolves an unqualified plan
    // (`schemaName=""`) into its default schema (`dbo` on MSSQL, `public` on
    // Postgres, none on SQLite), so the live row's schema is engine-specific while
    // the migration plan keeps its declared schema. Matching the dropped-tables
    // half of this same function, which already uses `liveNames.contains(key.table)`.
    for (auto const& t: liveTables)
    {
        if (t.name == "schema_migrations")
            continue;
        if (bookkeepingNamesSet.contains(t.name))
            continue;
        if (!intendedNames.contains(t.name))
            result.preservedTables.push_back(MakeFqtn(t.schema, t.name));
    }

    if (dryRun)
        return result;

    // Drop in batches and commit each one. A single transaction over hundreds of tables
    // exhausts Postgres's per-transaction lock pool (each `DROP TABLE ... CASCADE` takes
    // an AccessExclusiveLock and any locks from CASCADE-triggered drops, capped by
    // `max_locks_per_transaction * (max_connections + max_prepared_transactions)`).
    // 32 keeps us well under the default 64 per-transaction limit even when CASCADE
    // pulls in dependent objects.
    constexpr std::size_t dropBatchSize = 32;

    auto dropOne = [&](SqlSchema::FullyQualifiedTableName const& key) {
        auto const sqls = formatter.DropTable(key.schema, key.table, /*ifExists=*/true, /*cascade=*/true);
        for (auto const& sql: sqls)
            (void) stmt.ExecuteDirect(sql);
    };

    auto runBatch = [&](auto&& batch) {
        auto transaction = SqlTransaction { dm.Connection(), SqlTransactionMode::ROLLBACK };
        std::ranges::for_each(batch, dropOne);
        transaction.Commit();
    };

#if defined(__cpp_lib_ranges_chunk) && __cpp_lib_ranges_chunk >= 202202L
    for (auto&& batch: result.droppedTables | std::views::chunk(dropBatchSize))
        runBatch(batch);
#else
    // Fallback for stdlibs that do not yet ship `std::views::chunk` (C++23).
    auto const dropped = std::span { result.droppedTables };
    for (std::size_t offset = 0; offset < dropped.size(); offset += dropBatchSize)
    {
        auto const count = std::min(dropBatchSize, dropped.size() - offset);
        runBatch(dropped.subspan(offset, count));
    }
#endif

    if (liveNames.contains("schema_migrations"))
    {
        auto transaction = SqlTransaction { dm.Connection(), SqlTransactionMode::ROLLBACK };
        auto const sqls = formatter.DropTable(std::string_view {}, std::string_view { "schema_migrations" }, true, true);
        for (auto const& sql: sqls)
            (void) stmt.ExecuteDirect(sql);
        result.schemaMigrationsDropped = true;
        transaction.Commit();
    }

    // Drop any advisory-lock bookkeeping tables (`_lightweight_locks` on
    // SQLite). On engines with server-native advisory locks the list is
    // empty and this loop is a no-op. We drop these *after*
    // `schema_migrations` so a partial failure during the migration drop
    // pass doesn't strand the lock table; the same `IF EXISTS` semantics
    // make the operation idempotent when tooling reruns hard-reset.
    for (auto const& tableName: bookkeepingTableNames)
    {
        if (!liveNames.contains(std::string { tableName }))
            continue;
        auto transaction = SqlTransaction { dm.Connection(), SqlTransactionMode::ROLLBACK };
        auto const sqls = formatter.DropTable(std::string_view {}, tableName, /*ifExists=*/true, /*cascade=*/true);
        for (auto const& sql: sqls)
            (void) stmt.ExecuteDirect(sql);
        transaction.Commit();
    }

    return result;
}

namespace
{
    /// @brief One affected table's worth of upgrade context — gathered offline before
    /// any DDL runs so the executor below can iterate without touching the live DB.
    struct UnicodeUpgradePending
    {
        SqlSchema::FullyQualifiedTableName key;
        std::vector<MigrationManager::ColumnUpgradeEntry> columns;
        std::vector<SqlSchema::ForeignKeyConstraint> affectedFks;
    };

    /// @brief Walks one folded table's columns against the live schema and returns
    /// a `UnicodeUpgradePending` if any column qualifies. Returns `nullopt` when the
    /// table is unknown to the live DB or no columns drift.
    std::optional<UnicodeUpgradePending> ComputeUpgradeForTable(
        SqlSchema::FullyQualifiedTableName const& folded,
        MigrationManager::PlanFoldingResult::TableState const& foldedState,
        SqlSchema::Table const& live)
    {
        std::map<std::string, SqlSchema::Column const*> liveColumns;
        for (auto const& c: live.columns)
            liveColumns.emplace(c.name, &c);

        UnicodeUpgradePending p;
        p.key = folded;

        std::set<std::string> upgradeColumnNames;
        for (auto const& intendedCol: foldedState.columns)
        {
            auto const liveIt = liveColumns.find(intendedCol.name);
            if (liveIt == liveColumns.end())
                continue;
            if (!IsUnicodeUpgradeCandidate(liveIt->second->type, intendedCol.type))
                continue;
            p.columns.push_back(MigrationManager::ColumnUpgradeEntry {
                .table = folded,
                .column = intendedCol.name,
                .liveType = liveIt->second->type,
                .intendedType = intendedCol.type,
                .nullable = liveIt->second->isNullable,
            });
            upgradeColumnNames.insert(intendedCol.name);
        }

        if (p.columns.empty())
            return std::nullopt;

        auto const fkTouchesUpgradeCol = [&](std::vector<std::string> const& cols) {
            return std::ranges::any_of(cols, [&](std::string const& c) { return upgradeColumnNames.contains(c); });
        };
        for (auto const& fk: live.foreignKeys)
            if (fkTouchesUpgradeCol(fk.foreignKey.columns))
                p.affectedFks.push_back(fk);
        for (auto const& fk: live.externalForeignKeys)
            if (fkTouchesUpgradeCol(fk.primaryKey.columns))
                p.affectedFks.push_back(fk);

        return p;
    }

    /// @brief Rewrites every flagged column's type token inside one SQLite-stored
    /// `CREATE TABLE` body. Single-pass: types we cannot locate are left at their old
    /// declaration — the next migration touching that column will surface the drift.
    ///
    /// Type tokens may carry a parenthesised size (e.g. `VARCHAR(80)`); the scanner
    /// tracks paren nesting so the column-list close paren after the size is not
    /// mistaken for the end of the type token.
    std::string RewriteSqliteCreateTableTypes(std::string createSql,
                                              std::map<std::string, std::string> const& newTypeByColumn)
    {
        for (auto const& [col, newType]: newTypeByColumn)
        {
            auto const needle = std::format(R"("{}" )", col);
            auto const pos = createSql.find(needle);
            if (pos == std::string::npos)
                continue;
            auto const start = pos + needle.size();
            auto end = start;
            int parenDepth = 0;
            while (end < createSql.size())
            {
                char const c = createSql[end];
                if (parenDepth == 0 && (c == ' ' || c == ',' || c == ')'))
                    break;
                if (c == '(')
                    ++parenDepth;
                else if (c == ')')
                    --parenDepth;
                ++end;
            }
            createSql.replace(start, end - start, newType);
        }
        return createSql;
    }

    /// @brief Apply the SQLite-specific upgrade path. Each affected table gets a
    /// `RebuildSqliteTable` round-trip with a transformer that rewrites the stored
    /// `CREATE TABLE` body in-place. SQLite's lack of column-type ALTER means a full
    /// rebuild is the canonical recipe — see `RebuildSqliteTable` for the rationale.
    void ExecuteSqliteUpgrade(SqlConnection& connection,
                              SqlQueryFormatter const& formatter,
                              std::vector<UnicodeUpgradePending> const& pending)
    {
        for (auto const& p: pending)
        {
            std::map<std::string, std::string> newTypeByColumn;
            for (auto const& c: p.columns)
                newTypeByColumn.emplace(c.column, formatter.ColumnType(c.intendedType));

            RebuildSqliteTable(connection, p.key.table, [&](std::string createSql) {
                return RewriteSqliteCreateTableTypes(std::move(createSql), newTypeByColumn);
            });
        }
    }

    /// @brief Build the FK-drop commands for one affected table.
    [[nodiscard]] std::vector<SqlAlterTableCommand> BuildDropFkCommands(UnicodeUpgradePending const& p)
    {
        std::vector<SqlAlterTableCommand> dropCommands;
        dropCommands.reserve(p.affectedFks.size());
        for (auto const& fk: p.affectedFks)
            if (fk.foreignKey.columns.size() == 1)
                dropCommands.emplace_back(
                    SqlAlterTableCommands::DropForeignKey { .columnName = fk.foreignKey.columns.front() });
        return dropCommands;
    }

    /// @brief Build the ALTER-column commands for one affected table.
    [[nodiscard]] std::vector<SqlAlterTableCommand> BuildAlterColumnCommands(UnicodeUpgradePending const& p)
    {
        std::vector<SqlAlterTableCommand> alterCommands;
        alterCommands.reserve(p.columns.size());
        for (auto const& c: p.columns)
            alterCommands.emplace_back(SqlAlterTableCommands::AlterColumn {
                .columnName = c.column,
                .columnType = c.intendedType,
                .nullable = c.nullable ? SqlNullable::Null : SqlNullable::NotNull,
            });
        return alterCommands;
    }

    /// @brief Build the FK-re-add commands for one affected table.
    [[nodiscard]] std::vector<SqlAlterTableCommand> BuildAddFkCommands(UnicodeUpgradePending const& p)
    {
        std::vector<SqlAlterTableCommand> addCommands;
        addCommands.reserve(p.affectedFks.size());
        for (auto const& fk: p.affectedFks)
        {
            if (fk.foreignKey.columns.size() != 1)
                continue;
            addCommands.emplace_back(SqlAlterTableCommands::AddForeignKey {
                .columnName = fk.foreignKey.columns.front(),
                .referencedColumn =
                    SqlForeignKeyReferenceDefinition {
                        .tableName = fk.primaryKey.table.table,
                        .columnName = fk.primaryKey.columns.empty() ? std::string {} : fk.primaryKey.columns.front(),
                    },
            });
        }
        return addCommands;
    }

    /// @brief Apply the cross-backend upgrade path (everything that isn't SQLite).
    /// Per-table sequence: drop affected FKs → alter columns → re-add FKs. Each
    /// step renders via the formatter so the in-tree `AlterTable` codegen is the
    /// single source of truth for the dialect's ALTER syntax.
    void ExecuteGenericUpgrade(SqlStatement& stmt,
                               SqlQueryFormatter const& formatter,
                               std::vector<UnicodeUpgradePending> const& pending)
    {
        auto const execAlter =
            [&](std::string_view schema, std::string_view table, std::vector<SqlAlterTableCommand> const& commands) {
                if (commands.empty())
                    return;
                auto const sqls = formatter.AlterTable(schema, table, commands);
                for (auto const& sql: sqls)
                    (void) stmt.ExecuteDirect(sql);
            };

        for (auto const& p: pending)
        {
            execAlter(p.key.schema, p.key.table, BuildDropFkCommands(p));
            execAlter(p.key.schema, p.key.table, BuildAlterColumnCommands(p));
            execAlter(p.key.schema, p.key.table, BuildAddFkCommands(p));
        }
    }
} // namespace

MigrationManager::UnicodeUpgradeResult MigrationManager::UnicodeUpgradeTables(bool dryRun)
{
    UnicodeUpgradeResult result;
    result.wasDryRun = dryRun;

    auto& dm = GetDataMapper();
    auto const& formatter = dm.Connection().QueryFormatter();
    auto const fold = FoldRegisteredMigrations(formatter);

    auto stmt = SqlStatement { dm.Connection() };
    auto const liveTables = SqlSchema::ReadAllTables(stmt, std::string {}, std::string {});

    std::map<std::string, SqlSchema::Table const*> liveByName;
    for (auto const& t: liveTables)
        liveByName.emplace(t.name, &t);

    std::vector<UnicodeUpgradePending> pendingPerTable;
    for (auto const& folded: fold.creationOrder)
    {
        auto const it = liveByName.find(folded.table);
        if (it == liveByName.end())
            continue;
        auto upgrade = ComputeUpgradeForTable(folded, fold.tables.at(folded), *it->second);
        if (!upgrade)
            continue;
        for (auto const& c: upgrade->columns)
            result.columns.push_back(c);
        for (auto const& fk: upgrade->affectedFks)
            result.rebuiltForeignKeys.push_back(SqlCompositeForeignKeyConstraint {
                .columns = fk.foreignKey.columns,
                .referencedTableName = fk.primaryKey.table.table,
                .referencedColumns = fk.primaryKey.columns,
            });
        pendingPerTable.push_back(std::move(*upgrade));
    }

    if (dryRun || pendingPerTable.empty())
        return result;

    auto transaction = SqlTransaction { dm.Connection(), SqlTransactionMode::ROLLBACK };
    if (dm.Connection().ServerType() == SqlServerType::SQLITE)
        ExecuteSqliteUpgrade(dm.Connection(), formatter, pendingPerTable);
    else
        ExecuteGenericUpgrade(stmt, formatter, pendingPerTable);
    transaction.Commit();
    return result;
}

MigrationStatus MigrationManager::GetMigrationStatus() const
{
    MigrationStatus status {};

    auto const appliedIds = GetAppliedMigrationIds();
    auto const pending = GetPending();
    auto const mismatches = VerifyChecksums();

    status.appliedCount = appliedIds.size();
    status.pendingCount = pending.size();
    status.mismatchCount = mismatches.size();
    status.totalRegistered = _migrations.size();

    // Count unknown applied migrations (applied but not in registered list)
    for (auto const& id: appliedIds)
    {
        if (!GetMigration(id))
        {
            ++status.unknownAppliedCount;
        }
    }

    return status;
}

} // namespace Lightweight::SqlMigration
