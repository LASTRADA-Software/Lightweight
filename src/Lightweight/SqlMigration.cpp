// SPDX-License-Identifier: Apache-2.0

#include "DataBinder/SqlVariant.hpp"
#include "DataMapper/DataMapper.hpp"
#include "QueryFormatter/SQLiteFormatter.hpp"
#include "SqlBackup/Sha256.hpp"
#include "SqlConnection.hpp"
#include "SqlErrorDetection.hpp"
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
    _compatPolicy = [lhs = std::move(_compatPolicy),
                     rhs = std::move(policy)](MigrationBase const& m) {
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

std::vector<MigrationTimestamp> MigrationManager::GetAppliedMigrationIds() const
{
    auto result = std::vector<MigrationTimestamp> {};
    auto& dm = GetDataMapper();
    auto records = std::vector<SchemaMigration> {};

    try
    {
        records = dm.Query<SchemaMigration>().OrderBy("version", SqlResultOrdering::ASCENDING).All();
    }
    catch (SqlException const&)
    {
        return result;
    }

    for (auto const& record: records)
        result.emplace_back(MigrationTimestamp { record.version.Value() });

    return result;
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
    std::optional<SqlString<128>> MakeOptionalSqlString128(std::string_view value)
    {
        if (value.empty())
            return std::nullopt;
        return SqlString<128> { value };
    }

    std::optional<SqlString<1024>> MakeOptionalSqlString1024(std::string_view value)
    {
        if (value.empty())
            return std::nullopt;
        return SqlString<1024> { value };
    }

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
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::vector<std::string_view>> ExtractQuotedStrings(std::string_view directive,
                                                                                    size_t searchPos,
                                                                                    size_t expectedStrings)
    {
        std::vector<std::string_view> quoted;
        quoted.reserve(expectedStrings);
        while (quoted.size() < expectedStrings)
        {
            auto const openQuote = directive.find('"', searchPos);
            if (openQuote == std::string_view::npos)
                return std::nullopt;
            auto const closeQuote = directive.find('"', openQuote + 1);
            if (closeQuote == std::string_view::npos)
                return std::nullopt;
            quoted.push_back(directive.substr(openQuote + 1, closeQuote - openQuote - 1));
            searchPos = closeQuote + 1;
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

    [[nodiscard]] SqliteGuard BuildSqliteGuard(SqliteGuard::Kind kind, std::span<std::string_view const> quoted)
    {
        SqliteGuard guard {
            .kind = kind,
            .tableName = std::string { quoted[0] },
            .columnName = std::string { quoted[1] },
            .referencedTable = {},
            .referencedColumn = {},
            .columns = {},
            .referencedColumns = {},
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
        // and 4th are comma-joined column lists split by the consumer.
        size_t const expectedStrings =
            (*kind == SqliteGuard::Kind::AddForeignKey || *kind == SqliteGuard::Kind::AddCompositeForeignKey) ? 4 : 2;
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

    /// Fetch the stored `CREATE TABLE` SQL for a SQLite table.
    [[nodiscard]] std::string FetchSqliteCreateTableSql(SqlStatement& stmt, std::string_view tableName)
    {
        auto cursor =
            stmt.ExecuteDirect(std::format(R"(SELECT sql FROM sqlite_schema WHERE type='table' AND name='{}')", tableName));
        if (!cursor.FetchRow())
            throw std::runtime_error(std::format("SQLite rebuild: table '{}' not found in sqlite_schema", tableName));
        return cursor.GetColumn<std::string>(1);
    }

    /// Fetch the column names of a SQLite table in declared order.
    [[nodiscard]] std::vector<std::string> FetchSqliteColumnNames(SqlStatement& stmt, std::string_view tableName)
    {
        auto cursor = stmt.ExecuteDirect(std::format(R"(PRAGMA table_info("{}"))", tableName));
        std::vector<std::string> columns;
        while (cursor.FetchRow())
            columns.push_back(cursor.GetColumn<std::string>(2)); // column 2 is `name`
        return columns;
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
    /// text. The migration transaction covers all four steps for atomicity.
    ///
    /// Assumes SQLite's default `foreign_keys = OFF` session setting, which is standard
    /// during migrations — with enforcement on, `DROP TABLE orig` would succeed but the
    /// brief interval between DROP and RENAME would leave FKs in other tables temporarily
    /// dangling.
    void RebuildSqliteTable(SqlConnection& connection, std::string_view tableName, auto&& transformCreateSql)
    {
        auto stmt = SqlStatement { connection };
        auto const originalSql = FetchSqliteCreateTableSql(stmt, tableName);
        auto const columns = FetchSqliteColumnNames(stmt, tableName);
        if (columns.empty())
            throw std::runtime_error(std::format("SQLite rebuild: table '{}' has no columns", tableName));

        std::string const tmpName = std::string { tableName } + "__lw_rebuild";

        // Substitute the original table name with the temp name in the stored SQL. The
        // quoted form is the one Lightweight emits; the unquoted fallback covers tables
        // created outside the library.
        auto replaceFirst = [](std::string s, std::string_view from, std::string_view to) {
            if (auto const pos = s.find(from); pos != std::string::npos)
                s.replace(pos, from.size(), to);
            return s;
        };
        auto withTmpName = replaceFirst(originalSql, std::format(R"("{}")", tableName), std::format(R"("{}")", tmpName));
        if (withTmpName == originalSql) // unquoted fallback
            withTmpName = replaceFirst(originalSql, tableName, tmpName);

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

    /// Advance past a `(...)` group starting at the first `(` at or after `from`.
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
            if (s[scan] == '(')
                ++depth;
            else if (s[scan] == ')')
                --depth;
            ++scan;
        }
        return depth == 0 ? scan : std::string_view::npos;
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

    /// Execute a SQL script that may be prefixed with a SQLite runtime-guard sentinel.
    ///
    /// If the script carries a guard, perform the presence check first and skip the DDL
    /// body when the guard condition is already satisfied (or unsatisfied for DROP).
    /// Otherwise execute the script directly.
    void ExecuteScriptRespectingSqliteGuards(SqlStatement& stmt, SqlConnection& connection, std::string_view script)
    {
        auto const parsed = TryParseSqliteGuard(script);
        if (!parsed || !connection.QueryFormatter().RequiresTableRebuildForForeignKeyChange())
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
        return [&connection](MigrationRenderContext& ctx,
                              std::string_view schema,
                              std::string_view table) {
            auto const sql = schema.empty()
                ? std::format("SELECT COLUMN_NAME, DATA_TYPE, CHARACTER_MAXIMUM_LENGTH "
                              "FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_NAME = '{}'",
                              table)
                : std::format("SELECT COLUMN_NAME, DATA_TYPE, CHARACTER_MAXIMUM_LENGTH "
                              "FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = '{}' AND TABLE_NAME = '{}'",
                              schema, table);

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
                    auto const width =
                        CharacterWidthFromDataType(dataType, static_cast<std::size_t>(*maxLengthOpt));
                    if (width.value == 0)
                        continue;
                    ctx.columnWidths[{ std::string(schema), std::string(table), columnName }] = width;
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
    // Check if Down() is implemented before attempting revert
    if (!migration.HasDownImplementation())
    {
        throw std::runtime_error(std::format("Migration '{}' (timestamp {}) cannot be reverted: Down() is not implemented.",
                                             migration.GetTitle(),
                                             migration.GetTimestamp().value));
    }

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
    for (auto& migration: pendingMigrations)
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

        result.entries.push_back(ChecksumRewriteEntry { .timestamp = timestamp,
                                                        .title = migration->GetTitle(),
                                                        .oldChecksum = stored,
                                                        .newChecksum = computed });

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

void MigrationManager::MarkMigrationAsApplied(MigrationBase const& migration)
{
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

        std::erase_if(result.indexes, [&](SqlCreateIndexPlan const& idx) {
            return idx.schemaName == schema && idx.tableName == tableName;
        });

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
            return std::visit(::Lightweight::detail::overloaded {
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
    bool IsUnicodeUpgradeCandidate(SqlColumnTypeDefinition const& liveType,
                                    SqlColumnTypeDefinition const& intendedType)
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

    std::set<SqlSchema::FullyQualifiedTableName> intended;
    for (auto const& key: fold.creationOrder)
        intended.insert(key);

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
    for (auto const& t: liveTables)
    {
        if (t.name == "schema_migrations")
            continue;
        auto const key = MakeFqtn(t.schema, t.name);
        if (!intended.contains(key))
            result.preservedTables.push_back(key);
    }

    if (dryRun)
        return result;

    auto transaction = SqlTransaction { dm.Connection(), SqlTransactionMode::ROLLBACK };

    for (auto const& key: result.droppedTables)
    {
        auto const sqls = formatter.DropTable(key.schema, key.table, /*ifExists=*/true, /*cascade=*/true);
        for (auto const& sql: sqls)
            (void) stmt.ExecuteDirect(sql);
    }

    if (liveNames.contains("schema_migrations"))
    {
        auto const sqls = formatter.DropTable(std::string_view {}, std::string_view { "schema_migrations" }, true, true);
        for (auto const& sql: sqls)
            (void) stmt.ExecuteDirect(sql);
        result.schemaMigrationsDropped = true;
    }

    transaction.Commit();
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
                .referencedColumn = SqlForeignKeyReferenceDefinition {
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
        auto const execAlter = [&](std::string_view schema,
                                    std::string_view table,
                                    std::vector<SqlAlterTableCommand> const& commands) {
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
