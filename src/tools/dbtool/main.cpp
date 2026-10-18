// SPDX-License-Identifier: Apache-2.0

#include "BackupDiff.hpp"
#include "CommandAccess.hpp"
#include "Lightweight/SqlConnectInfo.hpp"
#include "ProgressText.hpp"
#include "StandardProgressManager.hpp"
#include "TaskProgressRenderer.hpp"
#include "UnicodeUpgradeReport.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlBackup/Common.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlLogger.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlScopedLock.hpp>
#include <Lightweight/Utils.hpp>
#include <Lightweight/Zip/ZipPath.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

#include <Config/BackupOutput.hpp>
#include <Config/ConfigDiscovery.hpp>
#include <Config/ConnectionTarget.hpp>
#include <Config/ProfileAdd.hpp>
#include <Config/ProfileFileEditor.hpp>
#include <Config/ProfilePassword.hpp>
#include <Config/ProfileStore.hpp>
#include <NoErrorDialogs.hpp>
#include <PluginDiscovery.hpp>
#include <PluginIngestion.hpp>
#include <PluginLoader.hpp>
#include <Secrets/ProfileCipher.hpp>
#include <Secrets/SecretResolver.hpp>
#include <Secrets/backends/StdinBackend.hpp>
#include <core/tui/SgrBuilder.hpp>
#include <core/tui/TerminalOutput.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

using namespace Lightweight;
using namespace Lightweight::SqlMigration;
using namespace std::string_view_literals;

namespace
{

/// @brief Emits a startup-trace breadcrumb to stderr when `DBTOOL_TRACE=1` is set.
///
/// Used to diagnose hangs that aren't visible from outside (e.g. the dbtool
/// process simply not exiting after `list-pending` against LocalDB on
/// Windows-2025 CI runners). Off by default — never appears in normal usage —
/// but invaluable when correlated with `sys.dm_exec_requests` and
/// `tasklist` snapshots from the on-timeout diagnostics in `test_dbtool.py`.
void TraceBreadcrumb(std::string_view label)
{
    static bool const enabled = []() {
        char const* const v = std::getenv("DBTOOL_TRACE");
        return v != nullptr && *v != '\0' && *v != '0';
    }();
    if (!enabled)
        return;
    std::println(std::cerr, "[dbtool-trace] {}", label);
    std::fflush(stderr);
}

/// Whether the process's standard output is a terminal, as core-cpp's terminal output asks the
/// operating system about it.
bool IsStdoutTerminal()
{
    return core::tui::TerminalOutput {}.isTerminal();
}

struct HelpColors
{
    std::string reset;
    std::string bold;
    std::string dim;
    std::string heading; // Section headings like "Commands:", "Options:"
    std::string command; // Command names
    std::string option;  // Option flags like --output
    std::string param;   // Parameter placeholders like <FILE>
    std::string example; // Example comments
    std::string code;    // Example code/commands

    /// How bright an SGR sequence draws its text.
    enum class Intensity : std::uint8_t
    {
        Normal,
        Bold,
        Dim,
    };

    /// Composes the SGR sequence for @p foreground at @p intensity.
    static std::string Sgr(core::tui::Color foreground, Intensity intensity)
    {
        auto style = core::tui::Style {};
        style.fg = foreground;
        style.bold = intensity == Intensity::Bold;
        style.dim = intensity == Intensity::Dim;
        return core::tui::buildSgrSequence(style);
    }

    static HelpColors Colored()
    {
        // The eight ANSI colours by their indexes in the terminal's palette.
        constexpr auto green = std::uint8_t { 2 };
        constexpr auto yellow = std::uint8_t { 3 };
        constexpr auto cyan = std::uint8_t { 6 };
        constexpr auto white = std::uint8_t { 7 };
        return {
            .reset = core::tui::buildSgrReset(),
            .bold = Sgr({}, Intensity::Bold),
            .dim = Sgr({}, Intensity::Dim),
            .heading = Sgr(cyan, Intensity::Bold),
            .command = Sgr(yellow, Intensity::Bold),
            .option = Sgr(green, Intensity::Bold),
            .param = Sgr(yellow, Intensity::Normal),
            .example = Sgr(white, Intensity::Dim),
            .code = Sgr(white, Intensity::Normal),
        };
    }

    static HelpColors Plain()
    {
        return {
            .reset = "",
            .bold = "",
            .dim = "",
            .heading = "",
            .command = "",
            .option = "",
            .param = "",
            .example = "",
            .code = "",
        };
    }
};

void PrintUsage()
{
    // clang-format off
    auto const c = IsStdoutTerminal() ? HelpColors::Colored() : HelpColors::Plain();

    std::println("{}Usage:{} {}dbtool{} <command> [options]\n", c.heading, c.reset, c.bold, c.reset);

    std::println("{}Commands:{}", c.heading, c.reset);
    std::println("  {}migrate{}                  Applies pending migrations", c.command, c.reset);
    std::println("  {}migrate-to-release{} {}<VERSION>{}  Applies pending migrations up to and including the named release",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}list-pending{}             Lists pending migrations", c.command, c.reset);
    std::println("  {}list-applied{}             Lists applied migrations", c.command, c.reset);
    std::println("  {}apply{} {}<TIMESTAMP>{}        Applies the migration with the given timestamp",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}rollback{} {}<TIMESTAMP>{}     Rolls back the migration with the given timestamp",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}rollback-to{} {}<TIMESTAMP>{}  Rolls back all migrations after the given timestamp",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}rollback-to-release{} {}<VERSION>{}  Rolls back all migrations after the given release",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}status{}                   Shows migration status summary", c.command, c.reset);
    std::println("  {}releases{}                 Lists declared releases and their migration counts/status",
                 c.command, c.reset);
    std::println("  {}mark-applied{} {}<TIMESTAMP>{} Marks a migration as applied without executing",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}rewrite-checksums{}         Rewrites schema_migrations.checksum to match current code",
                 c.command, c.reset);
    std::println("                            (use after a regen that changes only byte shape, not logic)");
    std::println("  {}hard-reset{}                Drops every migration-owned table (preserves user tables)",
                 c.command, c.reset);
    std::println("                            and the schema_migrations table; pair with `migrate`");
    std::println("  {}unicode-upgrade-tables{}    Rewrites legacy VARCHAR/CHAR columns to NVARCHAR/NCHAR",
                 c.command, c.reset);
    std::println("                            where the registered migrations now declare wide types");
    std::println("  {}exec{} {}<QUERY>{}             Executes the given SQL query and prints any result set.",
                 c.command, c.reset, c.param, c.reset);
    std::println("                            Pass `-` (or omit the argument) to read the query from stdin.");
    std::println("  {}backup{} [--output FILE]   Backs up the database to a file (see --output)", c.command, c.reset);
    std::println("  {}restore{} --input FILE     Restores the database from a file", c.command, c.reset);
    std::println("  {}backup-diff{} --left A --right B  Compares the row data of two backup archives",
                 c.command, c.reset);
    std::println("                            (order-independent; no DB connection needed)");
    std::println("  {}resolve-secret{} {}<REF>{}   Prints a resolved secret to stdout (env:, file:, stdin:)",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}list-profiles{}            Lists profiles from the configuration file (see --config)",
                 c.command, c.reset);
    std::println("  {}add-profile{}              Adds a profile to dbtool.yml with its password encrypted:",
                 c.command, c.reset);
    std::println("                            --name <NAME> (--connection-string <STR> | --dsn <DSN> [--uid <UID>])");
    std::println("                            [--schema <S>] [--plugins-dir <DIR>] [--backup-dir <DIR>]");
    std::println("                            [--no-password] [--set-default] [--force]. The password is");
    std::println("                            prompted for (or read from stdin).");
    std::println("");

    // Descriptions start at column 29 (longest option is 27 chars + 2 space minimum gap)
    std::println("{}Options:{}", c.heading, c.reset);
    std::println("  {}--plugins-dir{} {}<DIR>{}       Directory to scan for migration plugins",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            (default: current directory)");
    std::println("  {}--connection-string{} {}<STR>{} Connection string. The target is, first match wins:",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            --connection-string; --profile; the defaultProfile of --config;");
    std::println("                            SQL_CONNECTION_STRING / ODBC_CONNECTION_STRING; the defaultProfile");
    std::println("                            of a discovered dbtool.yml. Commands that write refuse the last one");
    std::println("                            and print their target before they start. Any command refuses it");
    std::println("                            too when a loaded plugin exports a post-init hook, which may write.");
    std::println("  {}--schema{} {}<NAME>{}           Database schema to use. For migrate/apply/rollback this becomes",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            the connection's default schema (PostgreSQL via SET search_path;");
    std::println("                            on SQL Server the login's server-side DEFAULT_SCHEMA is what");
    std::println("                            applies — Lightweight does not switch it per-session). For");
    std::println("                            backup/restore the value also qualifies table names in the archive.");
    std::println("                            `backup` takes a comma-separated list (`--schema dbo,sales`) and");
    std::println("                            then writes a multi-schema archive; `restore` of such an archive");
    std::println("                            restores each table into its own schema and refuses `--schema`.");
    std::println("  {}--all-schemas{}             Backup every schema that holds user tables (excludes sys,",
                 c.option, c.reset);
    std::println("                            INFORMATION_SCHEMA, guest, db_* and pg_*)");
    std::println("  {}--config{} {}<FILE>{}           Config file (default: nearest dbtool.yml above the current",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            or executable directory, then the per-user config, whose");
    std::println("                            folder LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR overrides)");
    std::println("  {}--profile{} {}<NAME>{}          Named profile from the config file (default: the file's",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            defaultProfile, see --connection-string for when it applies)");
    std::println("  {}--output{} {}<FILE>{}           Output file for backup. A bare file name, or no --output at all",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            (generates <profile>-<timestamp>.zip), goes into the profile's");
    std::println("                            `backupDir` / the file's `defaultBackupDir` when one is set; a");
    std::println("                            path with a directory part is used as given");
    std::println("  {}--input{} {}<FILE>{}            Input file for restore",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--left{} {}<FILE>{}             First backup archive for backup-diff (baseline)",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--right{} {}<FILE>{}            Second backup archive for backup-diff (candidate)",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--ignore-table{} {}<NAME>{}     backup-diff: report but don't fail on this table (repeatable;",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            for high-write tables that drift on a live DB). In a");
    std::println("                            multi-schema archive a table is <schema>/<table> (or");
    std::println("                            <schema>.<table>); a bare name must be unique across schemas");
    std::println("  {}--filter-tables{} {}<PATTERN>{} Tables to backup/restore (default: {} for all)",
                 c.option, c.reset, c.param, c.reset, "*");
    std::println("                            Comma-separated, supports wildcards ({} and {})", "*", "?");
    std::println("                            Examples: Users,Products,{}_log,dbo.Users", "*");
    std::println("  {}--jobs{} {}<N>{}                Number of concurrent jobs (default: 1)",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--max-retries{} {}<N>{}         Maximum retry attempts for transient errors (default: 3)",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--compression{} {}<METHOD>{}    Compression method for backup (default: deflate)",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            Methods: none, deflate, bzip2, lzma, zstd, xz");
    std::println("  {}--compression-level{} {}<N>{}   Compression level 0-9 (default: 6)",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--chunk-size{} {}<SIZE>{}       Chunk size for backup data (default: 10M)",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            Accepts: bytes, K/KB, M/MB, G/GB suffixes");
    std::println("  {}--memory-limit{} {}<SIZE>{}     Memory limit for restore (default: auto-detect)",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            Accepts: bytes, K/KB, M/MB, G/GB suffixes");
    std::println("  {}--batch-size{} {}<N>{}          Batch size for restore (default: auto-calculated)",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--progress{} {}<TYPE>{}         Progress output type: unicode (default), ascii, logline",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--dry-run{}, {}-n{}             Show what would be done without doing it (`apply` rejects it)",
                 c.option, c.reset, c.option, c.reset);
    std::println("  {}--no-lock{}                 Skip migration locking for write operations",
                 c.option, c.reset);
    std::println("  {}--schema-only{}             Backup/restore schema only, skip data",
                 c.option, c.reset);
    std::println("  {}--quiet{}                   Suppress progress output", c.option, c.reset);
    std::println("  {}--verbose{}, {}-v{}             Emit extra informational output (e.g. shadowed plugins)",
                 c.option, c.reset, c.option, c.reset);
    std::println("  {}--show-examples{}           Show usage examples and exit", c.option, c.reset);
    std::println("  {}--help{}                    Show this help message", c.option, c.reset);
    std::println("");
    std::println("Run {}dbtool --show-examples{} to see usage examples.", c.option, c.reset);
    // clang-format on
}

void PrintExamples()
{
    // clang-format off
    auto const c = IsStdoutTerminal() ? HelpColors::Colored() : HelpColors::Plain();

    std::println("{}Examples:{}", c.heading, c.reset);
    std::println("");

    std::println("  {}# Apply pending migrations using an SQLite database file via ODBC:{}", c.example, c.reset);
    std::println("  {}dbtool migrate --connection-string \"DRIVER=SQLite3;Database=test.db\"{}", c.code, c.reset);
    std::println("");

    std::println("  {}# List pending migrations for a specific plugins directory:{}", c.example, c.reset);
    std::println("  {}dbtool list-pending --plugins-dir ./plugins{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Rollback a specific migration:{}", c.example, c.reset);
    std::println("  {}dbtool rollback 20230101000000 --connection-string \"DRIVER=SQLite3;Database=test.db\"{}",
                 c.code, c.reset);
    std::println("");

    std::println("  {}# Migrate up to (and including) a named release:{}", c.example, c.reset);
    std::println("  {}dbtool migrate-to-release 1.0.0{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Preview the SQL that would run up to release 1.0.0:{}", c.example, c.reset);
    std::println("  {}dbtool migrate-to-release 1.0.0 --dry-run{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Backup entire database:{}", c.example, c.reset);
    std::println("  {}dbtool backup --output backup.zip --jobs 4{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Backup into the folder dbtool.yml names (defaultBackupDir / the profile's backupDir):{}",
                 c.example, c.reset);
    std::println("  {}dbtool backup --profile prod{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Backup specific tables by name:{}", c.example, c.reset);
    std::println("  {}dbtool backup --output backup.zip --filter-tables=Users,Products{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Backup tables matching wildcard patterns:{}", c.example, c.reset);
    std::println("  {}dbtool backup --output backup.zip --filter-tables=\"*_log,audit*\"{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Backup with schema-qualified table names:{}", c.example, c.reset);
    std::println("  {}dbtool backup --output backup.zip --filter-tables=dbo.Users,sales.*{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Backup two schemas, with foreign keys between them, into one archive:{}", c.example, c.reset);
    std::println("  {}dbtool backup --output backup.zip --schema dbo,sales{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Restore database from backup:{}", c.example, c.reset);
    std::println("  {}dbtool restore --input backup.zip --jobs 4{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Restore to a different schema:{}", c.example, c.reset);
    std::println("  {}dbtool restore --input backup.zip --schema new_schema{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Restore only specific tables:{}", c.example, c.reset);
    std::println("  {}dbtool restore --input backup.zip --filter-tables=Users,Products{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Backup schema only (no data):{}", c.example, c.reset);
    std::println("  {}dbtool backup --output schema.zip --schema-only{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Restore schema only (create empty tables):{}", c.example, c.reset);
    std::println("  {}dbtool restore --input backup.zip --schema-only{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Compare the data of two backup archives (detect silent corruption):{}", c.example, c.reset);
    std::println("  {}dbtool backup-diff --left backup_st.zip --right backup_mt.zip{}", c.code, c.reset);
    std::println("");

    std::println("  {}# List configured connection profiles (no DB connection needed):{}", c.example, c.reset);
    std::println("  {}dbtool list-profiles{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Add a profile; the password is prompted for and stored encrypted:{}", c.example, c.reset);
    std::println("  {}dbtool add-profile --name prod --dsn ACME_PROD --uid deploy --set-default{}", c.code, c.reset);
    std::println("");
    // clang-format on
}

enum class ProgressType : uint8_t
{
    Unicode,
    Ascii,
    Loglines,
    None
};

struct Options
{
    std::string command;
    std::string argument;
    /// Plugin search directories. `--plugins-dir` sets this to a single-element
    /// list; `defaultPluginsDir` in `dbtool.yml` may set it to multiple entries
    /// — see `Tools::DiscoverPlugins` for the filename-based dedup that
    /// reconciles the multi-directory case.
    std::vector<std::filesystem::path> pluginsDir;
    SqlConnectionString connectionString;
    std::string configFile;
    std::string profileName;                  ///< Named profile selected via --profile (empty = ProfileStore default)
    std::filesystem::path resolvedConfigPath; ///< Config file the profile was loaded from
    Lightweight::Config::ConfigSource configSource = Lightweight::Config::ConfigSource::None; ///< How it was found
    std::optional<Lightweight::Config::Profile> selectedProfile; ///< Profile that supplies the connection, if any
    /// Where `connectionString` came from; decided by `ResolveConnectionTargetOptions`.
    Lightweight::Config::ConnectionTargetSource targetSource = Lightweight::Config::ConnectionTargetSource::None;
    std::string_view targetEnvironmentVariable; ///< The variable that supplied the connection, if any
    std::string declaredDefaultProfile;         ///< The config file's `defaultProfile` as written, may be empty
    std::filesystem::path outputFile;
    std::filesystem::path backupDir; ///< Configured backup folder (the profile's `backupDir`, else `defaultBackupDir`)
    std::filesystem::path inputFile;
    std::filesystem::path leftFile;     ///< First archive for `backup-diff` (--left)
    std::filesystem::path rightFile;    ///< Second archive for `backup-diff` (--right)
    std::set<std::string> ignoreTables; ///< `backup-diff` tables to report-but-not-fail (--ignore-table)
    ProgressType progressType = ProgressType::Unicode;
    bool schemaSet = false;  ///< `--schema` was given on the command line (not taken from a profile)
    bool allSchemas = false; ///< `--all-schemas`: back up every schema that holds user tables
    unsigned jobs = 1;
    unsigned maxRetries = 3; ///< Maximum retry attempts for transient errors
    std::string schema;
    std::string filterTables = "*";            ///< Table filter for backup/restore (default: all tables)
    std::string compressionMethod = "deflate"; ///< Compression method for backup
    unsigned compressionLevel = 6;             ///< Compression level (0-9)
    std::string chunkSize = "10M";             ///< Chunk size for backup (supports K/M/G suffixes)
    std::string memoryLimit;                   ///< Memory limit for restore (supports K/M/G suffixes)
    std::string batchSize;                     ///< Batch size for restore (rows per batch)
    bool pluginsDirSet = false;
    bool connectionStringSet = false;
    bool dryRun = false;                 ///< If true, show what would be done without actually doing it
    bool noLock = false;                 ///< If true, skip migration locking for write operations
    bool schemaOnly = false;             ///< If true, backup/restore schema only (no data)
    bool yes = false;                    ///< If true, confirm destructive actions (e.g. rewrite-checksums)
    bool verbose = false;                ///< If true, emit extra informational output (e.g. shadowed plugins)
    bool profileIsFirstFallback = false; ///< The file's defaultProfile is unset or unknown; its first profile was used

    /// @brief `--up-to <X>` for migration commands. Empty = no bound.
    std::string upTo;

    // `add-profile` inputs.
    std::string newProfileName; ///< --name
    std::string dsn;            ///< --dsn
    std::string uid;            ///< --uid
    std::string newBackupDir;   ///< --backup-dir (`add-profile`)
    bool noPassword = false;    ///< --no-password: store the profile without a password
    bool setDefault = false;    ///< --set-default: make the new profile the default
    bool force = false;         ///< --force: replace an existing profile of the same name
};

/// Commands that never read a connection profile; `ResolveConnectionTargetOptions` is skipped for them.
inline constexpr auto CommandsWithoutProfile = std::array<std::string_view, 7> {
    "", "help", "show-examples", "add-profile", "resolve-secret", "list-profiles", "backup-diff",
};

/// Whether `command` reads a connection profile from dbtool.yml.
/// @param command The parsed command name.
/// @return False for the commands in `CommandsWithoutProfile`.
[[nodiscard]] bool CommandUsesProfile(std::string_view command) noexcept
{
    return !std::ranges::contains(CommandsWithoutProfile, command);
}

/// Environment variables that supply a connection string, in the order they are consulted.
inline constexpr auto ConnectionStringVariables = std::array<std::string_view, 2> {
    "SQL_CONNECTION_STRING",
    "ODBC_CONNECTION_STRING",
};

/// A connection string taken from the environment, and the variable it came from.
struct EnvironmentConnectionString
{
    std::string_view variable;
    std::string value;
};

/// Returns the first non-empty variable of `ConnectionStringVariables`, if any.
[[nodiscard]] std::optional<EnvironmentConnectionString> FindEnvironmentConnectionString()
{
    for (auto const variable: ConnectionStringVariables)
        if (auto const* value = std::getenv(std::string { variable }.c_str()); value != nullptr && *value != '\0')
            return EnvironmentConnectionString { .variable = variable, .value = value };
    return std::nullopt;
}

/// A loaded dbtool.yml and how it was found.
struct LoadedConfig
{
    Lightweight::Config::ProfileStore store;
    Lightweight::Config::DiscoveredConfig discovered;
};

/// Locates and loads the run's dbtool.yml (see `Config::FindConfigFile`). Exits on a missing
/// `--config` file or one that does not parse.
/// @param options Parsed options.
/// @return The file, or `std::nullopt` when none exists.
[[nodiscard]] std::optional<LoadedConfig> LoadConfig(Options const& options)
{
    namespace Cfg = Lightweight::Config;
    auto discovered = Cfg::FindConfigFile(Cfg::DefaultDiscoveryInputs(options.configFile));
    if (!discovered)
    {
        std::println(std::cerr, "Error: {}", discovered.error());
        std::exit(EXIT_FAILURE);
    }
    if (discovered->source == Cfg::ConfigSource::None)
        return std::nullopt;
    if (options.verbose)
        std::println(
            std::cerr, "Using config: {} (found via {})", discovered->path.string(), Cfg::ToString(discovered->source));

    auto store = Cfg::ProfileStore::LoadOrDefault(discovered->path);
    if (!store)
    {
        std::println(std::cerr, "Error loading config file: {}", store.error());
        std::exit(EXIT_FAILURE);
    }
    return LoadedConfig { .store = std::move(*store), .discovered = std::move(*discovered) };
}

/// Selects the profile named by `--profile`, else the file's default. Exits when `--profile`
/// names a profile that no file defines.
/// @param config  The loaded config, if any.
/// @param options Parsed options.
/// @return The profile, or null when there is none.
[[nodiscard]] Lightweight::Config::Profile const* SelectProfile(std::optional<LoadedConfig> const& config,
                                                                Options const& options)
{
    if (options.profileName.empty())
        return config ? config->store.Default() : nullptr; // legacy files synthesise a "default" profile

    auto const* profile = config ? config->store.Find(options.profileName) : nullptr;
    if (!profile)
    {
        std::println(std::cerr,
                     "Error: profile '{}' not found in {}.",
                     options.profileName,
                     config ? config->discovered.path.string() : std::string { "any dbtool.yml" });
        std::exit(EXIT_FAILURE);
    }
    return profile;
}

/// Fills the options the CLI left unset from the selected profile: plugins and backup folders, schema.
void ApplyProfileSettings(Lightweight::Config::ProfileStore const& store,
                          Lightweight::Config::Profile const& profile,
                          Options& options)
{
    if (!options.pluginsDirSet)
    {
        if (auto effective = store.EffectivePluginsDir(profile); !effective.empty())
            options.pluginsDir = std::move(effective);
    }
    options.backupDir = store.EffectiveBackupDir(profile);
    if (!profile.schema.empty() && options.schema.empty())
        options.schema = profile.schema;
}

/// Decides the run's connection target (see `Config::ResolveConnectionTarget` for the precedence)
/// and fills `options` from it: the connection string, and the selected profile's settings that
/// were not already set on the CLI. Supports both the legacy single-profile YAML shape (top-level
/// `PluginsDir` / `ConnectionString` / `Schema`) and the multi-profile shape via
/// `ProfileStore::LoadOrDefault`.
///
/// The profile's password is deliberately *not* resolved here (see
/// `ApplyProfilePassword`): commands that never connect must not prompt for it.
void ResolveConnectionTargetOptions(Options& options)
{
    namespace Cfg = Lightweight::Config;

    // `--connection-string` without `--profile` applies no profile at all: merging another
    // profile's `schema` / `pluginsDir` into a target the user typed is at best surprising
    // (`schema: dbo` from a SQL Server profile breaks every query against SQLite).
    auto const config = options.connectionStringSet && options.profileName.empty() ? std::nullopt : LoadConfig(options);
    auto const* profile = SelectProfile(config, options);
    auto const environment = FindEnvironmentConnectionString();
    if (config)
    {
        options.resolvedConfigPath = config->discovered.path;
        options.configSource = config->discovered.source;
    }

    auto const target = Cfg::ResolveConnectionTarget({
        .commandLineConnectionString = options.connectionStringSet,
        .explicitProfile = !options.profileName.empty(),
        .configSource = options.configSource,
        .profileHasConnection = profile != nullptr && profile->HasConnection(),
        .environmentConnectionString = environment.has_value(),
    });
    options.targetSource = target.source;
    options.profileIsFirstFallback = config && profile != nullptr && options.profileName.empty()
                                     && config->store.Find(config->store.DefaultProfileName()) == nullptr;
    if (config)
        options.declaredDefaultProfile = config->store.DefaultProfileName();

    if (config && profile != nullptr && target.appliesProfileSettings)
        ApplyProfileSettings(config->store, *profile, options);

    if (target.source == Cfg::ConnectionTargetSource::Environment && environment)
    {
        options.connectionString = SqlConnectionString { environment->value };
        options.targetEnvironmentVariable = environment->variable;
    }
    else if (Cfg::IsProfileSource(target.source) && profile != nullptr)
    {
        // Profiles keyed by DSN are flattened into an ODBC connection string; the password is
        // added later by `ApplyProfilePassword`.
        options.connectionString = SqlConnectionString { std::format("{}", profile->ToConnectInfo()) };
        options.selectedProfile = *profile;
    }
}

/// Names where the connection target came from, e.g. "profile 'prod' from D:/x/dbtool.yml (--config)".
/// @param options Options after `ResolveConnectionTargetOptions`.
/// @return The description, without any part of the connection string.
[[nodiscard]] std::string DescribeTargetOrigin(Options const& options)
{
    namespace Cfg = Lightweight::Config;
    switch (options.targetSource)
    {
        case Cfg::ConnectionTargetSource::CommandLine:
            return "--connection-string";
        case Cfg::ConnectionTargetSource::Environment:
            return std::format("environment variable {}", options.targetEnvironmentVariable);
        case Cfg::ConnectionTargetSource::ExplicitProfile:
        case Cfg::ConnectionTargetSource::ExplicitConfig:
        case Cfg::ConnectionTargetSource::DiscoveredConfig:
            return std::format("profile '{}' from {} ({})",
                               options.selectedProfile ? options.selectedProfile->name : std::string {},
                               options.resolvedConfigPath.string(),
                               options.configSource == Cfg::ConfigSource::Explicit
                                   ? std::string { "--config" }
                                   : std::format("discovered via {}", Cfg::ToString(options.configSource)));
        case Cfg::ConnectionTargetSource::None:
            break;
    }
    return "nowhere";
}

/// Reports why a run refuses a target that only a discovered dbtool.yml's default profile supplies.
/// @param options Options after `ResolveConnectionTargetOptions`.
/// @param reason  What would write, e.g. "it writes to the database".
/// @param remedy  An extra way out appended to the advice, or empty.
void ReportDiscoveredTargetRefusal(Options const& options, std::string_view reason, std::string_view remedy)
{
    namespace Cfg = Lightweight::Config;
    std::println(std::cerr,
                 "Error: refusing to run '{}' against {}: {}.\n"
                 "That target is only the default profile '{}'{} of {}, which was found automatically (via {}).\n"
                 "Choose a target with --profile <NAME>, --config <FILE> or --connection-string <STR>, or set "
                 "SQL_CONNECTION_STRING{}.",
                 options.command,
                 Cfg::DescribeConnectionTarget(options.connectionString),
                 reason,
                 options.selectedProfile ? options.selectedProfile->name : std::string {},
                 options.profileIsFirstFallback ? Cfg::FirstProfileFallbackNote(options.declaredDefaultProfile) : "",
                 options.resolvedConfigPath.string(),
                 Cfg::ToString(options.configSource),
                 remedy);
}

/// For a command that writes: refuses a target that only a discovered dbtool.yml's
/// default profile supplies, and otherwise prints the target before anything is written.
/// @param options Options after `ResolveConnectionTargetOptions`.
/// @return False when the command must not run.
[[nodiscard]] bool CheckWriteTarget(Options const& options)
{
    namespace Cfg = Lightweight::Config;
    if (!Tools::CommandWritesToDatabase(options.command, options.dryRun)
        || options.targetSource == Cfg::ConnectionTargetSource::None)
        return true;

    if (!Cfg::AllowsWrites(options.targetSource))
    {
        ReportDiscoveredTargetRefusal(options, "it writes to the database", "");
        return false;
    }

    std::println(std::cerr,
                 "Target: {} (from {})",
                 Cfg::DescribeConnectionTarget(options.connectionString),
                 DescribeTargetOrigin(options));
    return true;
}

/// Refuses, for any command, to run plugin post-init hooks against a target that only a
/// discovered dbtool.yml's default profile supplies: a hook may write (e.g. seed reference
/// data). The hooks are not skipped instead, as that could change what `status` reports.
/// @param options Options after `ResolveConnectionTargetOptions`.
/// @param plugins The loaded plugins.
/// @return False when the command must not run.
[[nodiscard]] bool CheckPostInitHookTarget(Options const& options, std::span<Tools::LoadedPlugin const> plugins)
{
    if (options.targetSource != Lightweight::Config::ConnectionTargetSource::DiscoveredConfig)
        return true;
    auto const withHook = Tools::PluginsWithPostInitHook(plugins);
    if (withHook.empty())
        return true;

    auto names = std::string {};
    for (auto const& path: withHook)
        names += std::format("{}{}", names.empty() ? "" : ", ", path.filename().string());
    ReportDiscoveredTargetRefusal(
        options,
        std::format("{} {} a post-init hook that can write to it", names, withHook.size() == 1 ? "runs" : "run"),
        ", or use a --plugins-dir without those plugins");
    return false;
}

/// Resolves the selected profile's password (decrypting `enc:` values, or via
/// `secretRef`) and folds it into `options.connectionString`. Called only for
/// commands that open a database connection.
/// @param options Parsed options; updated in place.
/// @return Where the password came from, or an error naming the profile.
[[nodiscard]] std::expected<Lightweight::Config::PasswordOrigin, std::string> ApplyProfilePassword(Options& options)
{
    namespace Cfg = Lightweight::Config;
    if (!options.selectedProfile)
        return Cfg::PasswordOrigin::None;

    auto const& profile = *options.selectedProfile;
    auto const resolver = Lightweight::Secrets::MakeDefaultResolver();
    return Cfg::ResolveProfilePassword(profile, Lightweight::Secrets::ProfileCipher::Builtin(), resolver)
        .transform([&](Cfg::ResolvedPassword const& password) {
            if (!password.value.empty())
                options.connectionString = SqlConnectionString { std::format("{}", profile.ToConnectInfo(password.value)) };
            return password.origin;
        });
}

/// Encrypts the selected profile's plaintext password in its dbtool.yml once a
/// probe connection has proven that it works. Never fails the command: a failed
/// probe is left for the command itself to report, and a failed rewrite only warns.
/// @param options Options after `ApplyProfilePassword`.
void EncryptPlaintextPasswordIfConnectable(Options const& options)
{
    namespace Cfg = Lightweight::Config;
    if (!options.selectedProfile)
        return;
    auto const& profile = *options.selectedProfile;
    {
        auto probe = SqlConnection { std::nullopt };
        if (!probe.Connect(options.connectionString))
            return;
    }

    auto const upgraded =
        Cfg::UpgradePlaintextPassword(options.resolvedConfigPath, profile, Lightweight::Secrets::ProfileCipher::Builtin());
    if (!upgraded)
    {
        std::println(std::cerr,
                     "Warning: could not encrypt the plaintext password of profile '{}': {}",
                     profile.name,
                     upgraded.error());
        return;
    }
    std::println(std::cerr,
                 "Encrypted the plaintext password of profile '{}' in {}.",
                 profile.name,
                 options.resolvedConfigPath.string());
    if (upgraded->insideGitWorkTree)
        std::println(std::cerr,
                     "Warning: {} is inside a git work tree; the old plaintext password may remain in its history. "
                     "Consider rotating the database password.",
                     options.resolvedConfigPath.string());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::expected<Options, std::string> ParseArguments(int argc, char** argv)
{
    Options options;

    if (auto const* termProgram = std::getenv("TERMINAL_NAME"); termProgram && termProgram == "contour"sv)
        options.progressType = ProgressType::Unicode;

    // First pass to get config file
    for (int i = 1; i < argc; ++i)
        if (argv[i] == "--config"sv && i + 1 < argc)
            options.configFile = argv[++i];

    // Initialize defaults before loading config
    options.pluginsDir = { std::filesystem::current_path() };

    // Second pass to get actual arguments and override config
    for (int i = 1; i < argc; ++i)
    {
        std::string const arg = argv[i];
        if (arg == "--help")
        {
            options.command = "help";
            break;
        }
        else if (arg == "--show-examples")
        {
            options.command = "show-examples";
            break;
        }
        else if (arg == "--plugins-dir")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --plugins-dir requires an argument" };
            options.pluginsDir = { std::filesystem::path { argv[++i] } };
            options.pluginsDirSet = true;
        }
        else if (arg == "--verbose" || arg == "-v")
        {
            options.verbose = true;
        }
        else if (arg == "--connection-string")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --connection-string requires an argument" };
            options.connectionString = SqlConnectionString { argv[++i] };
            options.connectionStringSet = true;
        }
        else if (arg == "--schema")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --schema requires an argument" };
            options.schema = argv[++i];
            options.schemaSet = !options.schema.empty();
        }
        else if (arg == "--profile")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --profile requires an argument" };
            options.profileName = argv[++i];
        }
        else if (arg == "--config")
        {
            i++; // Skip argument (already processed)
        }
        else if (arg == "--output")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --output requires an argument" };
            options.outputFile = argv[++i];
        }
        else if (arg == "--input")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --input requires an argument" };
            options.inputFile = argv[++i];
        }
        else if (arg == "--left")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --left requires an argument" };
            options.leftFile = argv[++i];
        }
        else if (arg.starts_with("--left="))
        {
            options.leftFile = arg.substr(7);
        }
        else if (arg == "--right")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --right requires an argument" };
            options.rightFile = argv[++i];
        }
        else if (arg.starts_with("--right="))
        {
            options.rightFile = arg.substr(8);
        }
        else if (arg == "--ignore-table")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --ignore-table requires an argument" };
            options.ignoreTables.insert(std::string { argv[++i] });
        }
        else if (arg.starts_with("--ignore-table="))
        {
            options.ignoreTables.insert(std::string { arg.substr(std::string_view { "--ignore-table=" }.size()) });
        }
        else if (arg == "--jobs")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --jobs requires an argument" };
            options.jobs = static_cast<unsigned>(std::stoi(argv[++i]));
        }
        else if (arg == "--max-retries")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --max-retries requires an argument" };
            options.maxRetries = static_cast<unsigned>(std::stoi(argv[++i]));
        }
        else if (arg == "--compression")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --compression requires an argument" };
            options.compressionMethod = argv[++i];
        }
        else if (arg.starts_with("--compression="))
        {
            options.compressionMethod = arg.substr(14);
        }
        else if (arg == "--compression-level")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --compression-level requires an argument" };
            options.compressionLevel = static_cast<unsigned>(std::stoi(argv[++i]));
        }
        else if (arg.starts_with("--compression-level="))
        {
            options.compressionLevel = static_cast<unsigned>(std::stoi(arg.substr(20)));
        }
        else if (arg == "--chunk-size")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --chunk-size requires an argument" };
            options.chunkSize = argv[++i];
        }
        else if (arg.starts_with("--chunk-size="))
        {
            options.chunkSize = arg.substr(13);
        }
        else if (arg == "--memory-limit")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --memory-limit requires an argument" };
            options.memoryLimit = argv[++i];
        }
        else if (arg.starts_with("--memory-limit="))
        {
            options.memoryLimit = arg.substr(15);
        }
        else if (arg == "--batch-size")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --batch-size requires an argument" };
            options.batchSize = argv[++i];
        }
        else if (arg.starts_with("--batch-size="))
        {
            options.batchSize = arg.substr(13);
        }
        else if (arg.starts_with("--filter-tables="))
        {
            options.filterTables = arg.substr(16);
        }
        else if (arg == "--filter-tables")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --filter-tables requires an argument" };
            options.filterTables = argv[++i];
        }
        else if (arg == "--progress")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --progress requires an argument (unicode, ascii, logline)" };
            std::string const value = argv[++i];
            if (value == "unicode")
                options.progressType = ProgressType::Unicode;
            else if (value == "ascii")
                options.progressType = ProgressType::Ascii;
            else if (value == "logline")
                options.progressType = ProgressType::Loglines;
            else
                return std::unexpected { std::format("Error: Unknown progress type '{}'. Use: unicode, ascii, logline",
                                                     value) };
        }
        else if (arg == "-q" || arg == "--quiet")
        {
            options.progressType = ProgressType::None;
        }
        else if (arg == "--dry-run" || arg == "-n")
        {
            options.dryRun = true;
        }
        else if (arg == "--no-lock")
        {
            options.noLock = true;
        }
        else if (arg == "--schema-only")
        {
            options.schemaOnly = true;
        }
        else if (arg == "--all-schemas")
        {
            options.allSchemas = true;
        }
        else if (arg == "--yes" || arg == "-y")
        {
            options.yes = true;
        }
        else if (arg == "--name")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --name requires an argument" };
            options.newProfileName = argv[++i];
        }
        else if (arg == "--dsn")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --dsn requires an argument" };
            options.dsn = argv[++i];
        }
        else if (arg == "--backup-dir")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --backup-dir requires an argument" };
            options.newBackupDir = argv[++i];
        }
        else if (arg == "--uid")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --uid requires an argument" };
            options.uid = argv[++i];
        }
        else if (arg == "--no-password")
        {
            options.noPassword = true;
        }
        else if (arg == "--set-default")
        {
            options.setDefault = true;
        }
        else if (arg == "--force")
        {
            options.force = true;
        }
        else if (arg == "--password" || arg.starts_with("--password=") || arg == "--pwd")
        {
            return std::unexpected { std::string {
                "Error: passwords are never accepted on the command line; dbtool add-profile prompts for it "
                "(or reads it from stdin)" } };
        }
        else if (arg == "--up-to")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --up-to requires an argument" };
            options.upTo = argv[++i];
        }
        else if (options.command.empty())
        {
            options.command = arg;
        }
        else if (options.argument.empty())
        {
            options.argument = arg;
        }
        else
        {
            return std::unexpected { std::format("Unknown argument: {}", arg) };
        }
    }
    return options;
}

/// Wires `--verbose` and dbtool's stdout/stderr conventions into
/// `Tools::PluginIngestOptions`. Extracted so the body of
/// `GetMigrationManager` stays scannable.
[[nodiscard]] Tools::PluginIngestOptions MakeIngestOptions(bool verbose)
{
    Tools::PluginIngestOptions opts;
    if (verbose)
        opts.logShadowed = [](std::string_view msg) {
            std::println(std::cerr, "{}", msg);
        };
    opts.logLoading = [](std::string_view msg) {
        std::println("{}", msg);
    };
    opts.logError = [](std::string_view msg) {
        std::println(std::cerr, "{}", msg);
    };
    opts.throwOnLoadError = true; // dbtool fails fast on bad plugins — historic behaviour
    return opts;
}

std::expected<MigrationBase const*, std::string> GetMigration(MigrationManager& manager, std::string_view argument)
{
    if (argument.empty())
        return std::unexpected("Migration timestamp is required.");

    auto ts = uint64_t {};
    auto const res = std::from_chars(argument.data(), argument.data() + argument.size(), ts);
    if (res.ec != std::errc {})
        return std::unexpected("Migration timestamp is invalid.");

    auto const* migration = manager.GetMigration(MigrationTimestamp { ts });
    if (!migration)
        return std::unexpected("Migration not found.");

    return migration;
}

/// What `SetupConnectionString` does when the target is a SQLite file that does not exist yet.
enum class MissingSqliteFile : std::uint8_t
{
    Create, ///< Create it, so a first `migrate` can start from an empty database.
    Leave,  ///< Leave the file system alone (a target dbtool must not write to).
};

/// Installs the resolved connection string as the process default.
/// @param connectionString The string `ResolveConnectionTargetOptions` chose, from the CLI,
///        a profile or the environment.
/// @param missingSqliteFile Whether a missing SQLite database file is created.
/// @return False, after reporting it, when there is none.
bool SetupConnectionString(SqlConnectionString const& connectionString, MissingSqliteFile missingSqliteFile)
{
    TraceBreadcrumb("SetupConnectionString: entered");
    if (connectionString.value.empty())
    {
        std::println(std::cerr,
                     "Error: No connection string provided. Use --connection-string, set SQL_CONNECTION_STRING "
                     "environment variable, or configure a profile in dbtool.yml (see `dbtool list-profiles`).\n");
        return false;
    }
    SqlConnection::SetDefaultConnectionString(connectionString);
    if (missingSqliteFile == MissingSqliteFile::Leave)
        return true;
    TraceBreadcrumb("SetupConnectionString: looking up default connection string");
    auto const& defaultString = SqlConnection::DefaultConnectionString();
    TraceBreadcrumb("SetupConnectionString: ensuring sqlite database file exists");
    if (!EnsureSqliteDatabaseFileExists(defaultString))
        std::println(std::cerr, "Warning: could not ensure SQLite database file exists for {}", defaultString.Sanitized());
    TraceBreadcrumb("SetupConnectionString: returning true");
    return true;
}

/// Builds the redacted, truncated `CONNECTION` column for `list-profiles`.
/// Extracted as a free function so `ListProfiles` stays under the clang-tidy
/// cognitive-complexity threshold (lambdas count against the enclosing
/// function's budget).
[[nodiscard]] std::string BuildProfileConnectionColumn(Lightweight::Config::Profile const& profile)
{
    constexpr std::size_t MaxConnectionWidth = 60;

    std::string s;
    if (!profile.connectionString.empty())
        s = SqlConnectionString::SanitizePwd(profile.connectionString);
    else if (!profile.dsn.empty())
        s = std::format("dsn={}", profile.dsn);

    if (s.size() > MaxConnectionWidth)
        s = std::format("{}...", std::string_view { s }.substr(0, MaxConnectionWidth - 3));
    return s;
}

/// Describes how a profile authenticates for the `AUTH` column of `list-profiles`,
/// without revealing the secret itself.
[[nodiscard]] std::string_view BuildProfileAuthColumn(Lightweight::Config::Profile const& profile)
{
    if (!profile.password.empty())
        return Lightweight::Secrets::ProfileCipher::IsEncrypted(profile.password) ? "encrypted" : "plaintext";
    if (!profile.secretRef.empty())
        return "secretRef";
    return "-";
}

/// Joins a profile's effective plugin search directories into a single
/// `PLUGINSDIR` column. The list separator mirrors `$PATH` conventions for
/// the platform so users recognise the form (`;` on Windows, `:` on POSIX).
[[nodiscard]] std::string BuildProfilePluginsDirColumn(Lightweight::Config::ProfileStore const& store,
                                                       Lightweight::Config::Profile const& profile)
{
    constexpr std::string_view PathSep =
#ifdef _WIN32
        ";";
#else
        ":";
#endif
    auto const effectiveDirs = store.EffectivePluginsDir(profile);
    std::string joined;
    for (std::size_t idx = 0; idx < effectiveDirs.size(); ++idx)
    {
        if (idx > 0)
            joined += PathSep;
        joined += effectiveDirs[idx].string();
    }
    return joined;
}

/// Implements the `list-profiles` command: enumerates every profile parsed by
/// `Lightweight::Config::ProfileStore` and prints a compact table with the
/// fields that are safe to display. `PWD=`/`Password=` values inside a profile's
/// raw `connectionString` are redacted via `SqlConnectionString::SanitizePwd`.
///
/// Does not open a database connection — runs alongside `resolve-secret` /
/// `help` / `show-examples` in the early dispatch block.
int ListProfiles(Options const& options)
{
    namespace Cfg = Lightweight::Config;

    struct LoadedConfig
    {
        Cfg::ProfileStore store;
        Cfg::DiscoveredConfig discovered;
    };

    auto const loadedConfig =
        Cfg::FindConfigFile(Cfg::DefaultDiscoveryInputs(options.configFile))
            .and_then([](Cfg::DiscoveredConfig discovered) -> std::expected<LoadedConfig, std::string> {
                if (discovered.source == Cfg::ConfigSource::None)
                    return LoadedConfig { .store = {}, .discovered = std::move(discovered) };
                return Cfg::ProfileStore::LoadOrDefault(discovered.path)
                    .transform([&](Cfg::ProfileStore s) {
                        return LoadedConfig { .store = std::move(s), .discovered = std::move(discovered) };
                    })
                    .transform_error([](std::string e) { return std::format("Loading config file: {}", e); });
            });
    if (!loadedConfig)
    {
        std::println(std::cerr, "Error: {}", loadedConfig.error());
        return EXIT_FAILURE;
    }
    if (loadedConfig->discovered.source == Cfg::ConfigSource::None)
    {
        std::println("No profile configuration found (searched for {} above the current and executable "
                     "directories, then {}).",
                     Cfg::ConfigFileName,
                     loadedConfig->discovered.path.string());
        return EXIT_SUCCESS;
    }
    auto const& store = loadedConfig->store;
    auto const& configPath = loadedConfig->discovered.path;
    if (store.Empty())
    {
        std::println("No profiles configured in {}.", configPath.string());
        return EXIT_SUCCESS;
    }

    // Mirrors ProfileStore::Default(): an unset DefaultProfileName falls back to
    // the first profile so that legacy single-profile files still show a marker.
    auto const* defaultProfile = store.Default();
    auto const defaultName = defaultProfile != nullptr ? defaultProfile->name : std::string {};

    struct Row
    {
        std::string name;
        std::string defaultMarker;
        std::string connection;
        std::string auth;
        std::string schema;
        std::string pluginsDir;
        std::string backupDir;
    };

    std::vector<Row> rows;
    rows.reserve(store.Profiles().size());
    for (auto const& profile: store.Profiles())
        rows.push_back(Row {
            .name = profile.name,
            .defaultMarker = profile.name == defaultName ? std::string { "*" } : std::string {},
            .connection = BuildProfileConnectionColumn(profile),
            .auth = std::string { BuildProfileAuthColumn(profile) },
            .schema = profile.schema,
            .pluginsDir = BuildProfilePluginsDirColumn(store, profile),
            .backupDir = store.EffectiveBackupDir(profile).string(),
        });

    auto const widthOf = [&](std::string Row::* member, std::string_view header) {
        auto width = header.size();
        for (auto const& row: rows)
            width = std::max(width, (row.*member).size());
        return width;
    };

    auto const nameWidth = widthOf(&Row::name, "NAME");
    auto const defaultWidth = widthOf(&Row::defaultMarker, "DEFAULT");
    auto const connectionWidth = widthOf(&Row::connection, "CONNECTION");
    auto const authWidth = widthOf(&Row::auth, "AUTH");
    auto const schemaWidth = widthOf(&Row::schema, "SCHEMA");
    auto const pluginsDirWidth = widthOf(&Row::pluginsDir, "PLUGINSDIR");

    auto const c = IsStdoutTerminal() ? HelpColors::Colored() : HelpColors::Plain();

    std::println("Profiles (from {}, found via {}):", configPath.string(), Cfg::ToString(loadedConfig->discovered.source));
    std::println("");
    std::println("{}{:<{}}  {:<{}}  {:<{}}  {:<{}}  {:<{}}  {:<{}}  {}{}",
                 c.heading,
                 "NAME",
                 nameWidth,
                 "DEFAULT",
                 defaultWidth,
                 "CONNECTION",
                 connectionWidth,
                 "AUTH",
                 authWidth,
                 "SCHEMA",
                 schemaWidth,
                 "PLUGINSDIR",
                 pluginsDirWidth,
                 "BACKUPDIR",
                 c.reset);

    for (auto const& row: rows)
        std::println("{:<{}}  {:<{}}  {:<{}}  {:<{}}  {:<{}}  {:<{}}  {}",
                     row.name,
                     nameWidth,
                     row.defaultMarker,
                     defaultWidth,
                     row.connection,
                     connectionWidth,
                     row.auth,
                     authWidth,
                     row.schema,
                     schemaWidth,
                     row.pluginsDir,
                     pluginsDirWidth,
                     row.backupDir);

    return EXIT_SUCCESS;
}

/// Implements `add-profile`: writes a new profile into dbtool.yml with its password
/// encrypted, keeping the rest of the file (comments included) untouched. The
/// password is read from a no-echo prompt, or one line of stdin when piped — never argv.
int AddProfileCommand(Options const& options)
{
    namespace Cfg = Lightweight::Config;

    if (options.newProfileName.empty())
    {
        std::println(std::cerr, "Error: add-profile requires --name <NAME>.");
        return EXIT_FAILURE;
    }
    if (options.connectionStringSet == !options.dsn.empty()) // both, or neither
    {
        std::println(std::cerr, "Error: add-profile requires exactly one of --connection-string or --dsn.");
        return EXIT_FAILURE;
    }

    auto const target = options.configFile.empty()
                            ? Cfg::FindConfigFile(Cfg::DefaultDiscoveryInputs()).transform([](Cfg::DiscoveredConfig found) {
                                  return std::move(found.path);
                              })
                            : std::expected<std::filesystem::path, std::string> { options.configFile };
    if (!target)
    {
        std::println(std::cerr, "Error: {}", target.error());
        return EXIT_FAILURE;
    }

    auto request = Cfg::ProfileRequest {
        .name = options.newProfileName,
        .connectionString = options.connectionStringSet ? options.connectionString.value : std::string {},
        .dsn = options.dsn,
        .uid = options.uid,
        .schema = options.schema,
        .pluginsDir = options.pluginsDirSet ? options.pluginsDir.front().string() : std::string {},
        .password = {},
        .backupDir = options.newBackupDir,
    };
    if (options.connectionStringSet && !Cfg::SplitInlinePassword(request.connectionString).password.empty())
        std::println(std::cerr, "Note: the password in --connection-string will be stored encrypted instead.");
    else if (!options.noPassword)
    {
        auto entered = Lightweight::Secrets::StdinBackend {}.Read(options.newProfileName);
        if (!entered)
        {
            std::println(std::cerr, "Error: no password was entered (use --no-password for a profile without one).");
            return EXIT_FAILURE;
        }
        request.password = std::move(*entered);
    }

    auto const written = Cfg::AddProfileToFile(*target,
                                               request,
                                               options.force ? Cfg::ReplaceExisting::Yes : Cfg::ReplaceExisting::No,
                                               options.setDefault ? Cfg::MakeDefault::Yes : Cfg::MakeDefault::No,
                                               Lightweight::Secrets::ProfileCipher::Builtin());
    if (!written)
    {
        std::println(std::cerr, "Error: {}", written.error());
        return EXIT_FAILURE;
    }

    std::println("Added profile '{}' to {}.", request.name, target->string());
    return EXIT_SUCCESS;
}

int ListPendingMigrations(MigrationManager& manager)
{
    TraceBreadcrumb("ListPendingMigrations: calling GetPending");
    auto const pending = manager.GetPending();
    TraceBreadcrumb("ListPendingMigrations: GetPending returned");
    std::println("Pending Migrations:");
    for (auto const* m: pending)
        std::println("  {} - {}", m->GetTimestamp().value, m->GetTitle());

    return EXIT_SUCCESS;
}

int ListAppliedMigrations(MigrationManager& manager)
{
    auto const appliedIds = manager.GetAppliedMigrationIds();

    std::println("Applied Migrations:");

    for (auto const& id: appliedIds)
        if (auto const* m = manager.GetMigration(id))
            std::println("  {} - {}", id.value, m->GetTitle());
        else
            std::println("  {} - (Unknown Migration)", id.value);

    return EXIT_SUCCESS;
}

int Status(MigrationManager& manager);

/// Describes the rows `IfNotExists` inserts skipped since @p skippedBefore, for a summary line.
/// @param manager       The migration manager that ran the command.
/// @param skippedBefore The manager's skipped-row count before the command ran.
/// @return `, skipped N existing row(s)` when rows were skipped, otherwise empty.
std::string SkippedRowsSuffix(MigrationManager const& manager, std::size_t skippedBefore)
{
    auto const skipped = manager.SkippedExistingRowCount() - skippedBefore;
    if (skipped == 0)
        return {};
    return std::format(", skipped {} existing row(s)", skipped);
}

/// Prints the manager's status messages to stdout, dbtool's historical output.
void InstallStdoutLogSink(MigrationManager& manager)
{
    manager.SetLogSink([](std::string_view message) { std::println("{}", message); });
}

/// The task renderer of a migrating command, drawing as `--progress` (or `--quiet`) says.
///
/// Output that is not a terminal gets log lines, since the animated styles write cursor escapes.
/// A dry run draws to stderr, so its stdout stays the SQL plan.
class CommandProgress
{
  public:
    explicit CommandProgress(Options const& options)
    {
        auto const requested = [&] {
            switch (options.progressType)
            {
                case ProgressType::Unicode:
                    return Tools::TaskProgressStyle::Unicode;
                case ProgressType::Ascii:
                    return Tools::TaskProgressStyle::Ascii;
                case ProgressType::Loglines:
                    return Tools::TaskProgressStyle::LogLine;
                case ProgressType::None:
                    return Tools::TaskProgressStyle::None;
            }
            std::unreachable();
        }();
        auto& stream = options.dryRun ? std::cerr : std::cout;
        auto const destination = Tools::DestinationOf(stream);
        auto const animated = requested == Tools::TaskProgressStyle::Unicode || requested == Tools::TaskProgressStyle::Ascii;
        _style = animated && destination == Tools::ProgressDestination::NotTerminal ? Tools::TaskProgressStyle::LogLine
                                                                                    : requested;
        _renderer = std::make_unique<Tools::TaskProgressRenderer>(_style, stream, destination);
    }

    /// @return The task the command reports to.
    [[nodiscard]] ProgressTask& Task() const noexcept
    {
        return *_renderer;
    }

    /// @return True when the manager's messages must go through the task to not break the bars.
    [[nodiscard]] bool RoutesMessagesThroughTask() const noexcept
    {
        return _style == Tools::TaskProgressStyle::Unicode || _style == Tools::TaskProgressStyle::Ascii;
    }

    /// Ends the rendering, so the command can print below the final frame.
    void Finish() noexcept
    {
        _renderer.reset();
    }

  private:
    Tools::TaskProgressStyle _style = Tools::TaskProgressStyle::None;
    std::unique_ptr<Tools::TaskProgressRenderer> _renderer;
};

/// Routes the manager's status messages through a task while it lives, so they print above the task
/// bars instead of breaking them; the stdout sink comes back afterwards.
class ScopedProgressLogSink
{
  public:
    ScopedProgressLogSink(MigrationManager& manager, ProgressTask& progress):
        _manager { manager }
    {
        _manager.SetLogSink([&progress](std::string_view message) { progress.Log(ProgressTask::LogLevel::Info, message); });
    }
    ScopedProgressLogSink(ScopedProgressLogSink const&) = delete;
    ScopedProgressLogSink(ScopedProgressLogSink&&) = delete;
    ScopedProgressLogSink& operator=(ScopedProgressLogSink const&) = delete;
    ScopedProgressLogSink& operator=(ScopedProgressLogSink&&) = delete;
    ~ScopedProgressLogSink()
    {
        InstallStdoutLogSink(_manager);
    }

  private:
    MigrationManager& _manager;
};

/// Runs the pre-migrate steps of a migrating command, when there are any.
/// @return True to go on with the migrations.
[[nodiscard]] bool RunPreMigrateStepsOf(MigrationManager& manager, CommandProgress const& progress, ExecutionMode mode)
{
    if (manager.GetPreMigrateSteps().empty())
        return true;
    auto const sink =
        progress.RoutesMessagesThroughTask() ? std::make_unique<ScopedProgressLogSink>(manager, progress.Task()) : nullptr;
    return manager.RunPreMigrateSteps(progress.Task(), mode) == PreMigrateStepsOutcome::Completed;
}

/// The applying phase of a migrating command. Without pre-migrate steps it prints the historical
/// `[i/n] Applying ...` lines; with steps it shows a "Migrations" task that names each migration
/// before it is applied, and routes the manager's messages like `RunPreMigrateStepsOf`.
class MigrationsPhase
{
  public:
    MigrationsPhase(MigrationManager& manager, CommandProgress const& progress)
    {
        if (manager.GetPreMigrateSteps().empty())
            return;
        if (progress.RoutesMessagesThroughTask())
            _sink = std::make_unique<ScopedProgressLogSink>(manager, progress.Task());
        _task = progress.Task().BeginSubTask("Migrations", 0);
    }

    /// Reports that migration @p index of @p count is about to be applied.
    void Starting(MigrationBase const& migration, std::size_t index, std::size_t count)
    {
        if (!_task)
        {
            std::println("[{}/{}] Applying {} {}", index + 1, count, migration.GetTimestamp().value, migration.GetTitle());
            return;
        }
        if (index == 0)
            _task->SetTotal(count);
        _task->Advance(index == 0 ? 0 : 1, std::format("{} {}", migration.GetTimestamp().value, migration.GetTitle()));
    }

    /// Names the only migration of a command that applies a single one; prints nothing without a task.
    void StartingSingle(MigrationBase const& migration)
    {
        if (!_task)
            return;
        _task->SetTotal(1);
        _task->Advance(0, std::format("{} {}", migration.GetTimestamp().value, migration.GetTitle()));
    }

    /// Marks the last started migration done.
    void Done()
    {
        if (_task)
            _task->Advance(1);
    }

    /// Runs @p apply; when it throws, marks the task failed with the error and rethrows.
    /// @return What @p apply returns.
    template <typename Apply>
    decltype(auto) Run(Apply&& apply)
    {
        try
        {
            return std::forward<Apply>(apply)();
        }
        catch (std::exception const& error)
        {
            if (_task)
                _task->Fail(error.what());
            throw;
        }
    }

    /// Ends the task and restores the stdout sink, so the command can print its summary.
    void Close()
    {
        _task.reset();
        _sink.reset();
    }

  private:
    std::unique_ptr<ScopedProgressLogSink> _sink;
    std::unique_ptr<ProgressTask> _task;
};

int Migrate(MigrationManager& manager, CommandProgress const& progress, ExecutionMode mode)
{
    if (!RunPreMigrateStepsOf(manager, progress, mode))
        return EXIT_FAILURE;

    if (mode == ExecutionMode::DryRun)
    {
        std::println("-- Dry run: Showing SQL statements that would be executed\n");
        auto statements = manager.PreviewPendingMigrations([](MigrationBase const& m, size_t i, size_t n) {
            std::println("-- [{}/{}] Migration: {} - {}", i + 1, n, m.GetTimestamp().value, m.GetTitle());
        });

        if (statements.empty())
        {
            std::println("-- No pending migrations.");
        }
        else
        {
            for (auto const& sql: statements)
                std::println("{};", sql);
            std::println("\n-- Total: {} SQL statements", statements.size());
        }
        return EXIT_SUCCESS;
    }

    std::println("Applying pending migrations...");
    auto const skippedBefore = manager.SkippedExistingRowCount();
    auto phase = MigrationsPhase { manager, progress };
    size_t const count = phase.Run([&] {
        return manager.ApplyPendingMigrations(
            [&phase](MigrationBase const& m, size_t i, size_t n) { phase.Starting(m, i, n); });
    });
    if (count > 0)
        phase.Done();
    phase.Close();
    if (count == 0)
        std::println("Database is already up to date. No migrations to apply.");
    else
        std::println("Successfully applied {} migration(s){}.", count, SkippedRowsSuffix(manager, skippedBefore));
    std::println("");
    return Status(manager);
}

/// Returns the exit code a command produced, or reports its error and returns EXIT_FAILURE.
/// @param result The command's exit code, or the message describing why it failed.
int ExitCodeOf(std::expected<int, std::string> const& result)
{
    if (result)
        return *result;
    std::println(std::cerr, "Error: {}", result.error());
    return EXIT_FAILURE;
}

int ApplyMigration(MigrationManager& manager, CommandProgress const& progress, std::string_view argument)
{
    return ExitCodeOf(GetMigration(manager, argument).and_then([&manager, &progress](MigrationBase const* migration) {
        if (!RunPreMigrateStepsOf(manager, progress, ExecutionMode::Execute))
            return std::expected<int, std::string> { EXIT_FAILURE };
        auto const skippedBefore = manager.SkippedExistingRowCount();
        {
            auto phase = MigrationsPhase { manager, progress };
            phase.StartingSingle(*migration);
            phase.Run([&] { manager.ApplySingleMigration(*migration); });
            phase.Done();
        }
        std::println("Applied migration {}{}.", migration->GetTimestamp().value, SkippedRowsSuffix(manager, skippedBefore));
        return std::expected<int, std::string> { EXIT_SUCCESS };
    }));
}

int RollbackMigration(MigrationManager& manager, std::string_view argument)
{
    return ExitCodeOf(GetMigration(manager, argument).and_then([&manager](MigrationBase const* migration) {
        auto const skippedBefore = manager.SkippedExistingRowCount();
        manager.RevertSingleMigration(*migration);
        std::println(
            "Rolled back migration {}{}.", migration->GetTimestamp().value, SkippedRowsSuffix(manager, skippedBefore));
        return std::expected<int, std::string> { EXIT_SUCCESS };
    }));
}

/// Displays a summary of migration status including applied, pending, and checksum mismatches.
///
/// @param manager The migration manager instance.
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
/// Latest release whose migrations have at least one applied entry, plus how many
/// of that release's registered migrations are applied (`applied` ≤ `total`). When
/// no release has any applied migration, `latest == nullptr`.
struct LatestReleaseStatus
{
    MigrationRelease const* latest;
    size_t applied;
    size_t total;
};

/// Walks registered releases in ascending order, looking up applied state per release,
/// and returns the *latest* release that has any applied migration in it (fully or
/// partially). Releases after that point (with zero applied migrations) are ignored
/// — they're pending work, not "applied-but-later".
LatestReleaseStatus ComputeLatestAppliedRelease(MigrationManager& manager)
{
    auto const& releases = manager.GetAllReleases();
    LatestReleaseStatus result { .latest = nullptr, .applied = 0, .total = 0 };
    if (releases.empty())
        return result;

    std::unordered_set<uint64_t> appliedSet;
    for (auto const& id: manager.GetAppliedMigrationIds())
        appliedSet.insert(id.value);

    for (auto const& release: releases)
    {
        auto const migs = manager.GetMigrationsForRelease(release.version);
        size_t appliedHere = 0;
        for (auto const* m: migs)
            if (appliedSet.contains(m->GetTimestamp().value))
                ++appliedHere;
        if (appliedHere > 0)
        {
            result.latest = &release;
            result.applied = appliedHere;
            result.total = migs.size();
        }
    }
    return result;
}

int Status(MigrationManager& manager)
{
    if (manager.GetAllMigrations().empty())
    {
        std::println(std::cerr,
                     "Error: No migrations registered. Ensure a migration plugin is available "
                     "via --plugins-dir (default: current directory).");
        return EXIT_FAILURE;
    }

    auto const status = manager.GetMigrationStatus();
    auto const mismatches = manager.VerifyChecksums();

    constexpr auto labelWidth = 26;

    std::println("Migration Status:");
    std::println("");
    std::println("  {:<{}}{}", "Registered migrations:", labelWidth, status.totalRegistered);
    std::println("  {:<{}}{}", "Applied migrations:", labelWidth, status.appliedCount);
    std::println("  {:<{}}{}", "Pending migrations:", labelWidth, status.pendingCount);

    if (status.unknownAppliedCount > 0)
    {
        std::println("  {:<{}}{} (applied but not registered)", "Unknown applied:", labelWidth, status.unknownAppliedCount);
    }

    if (auto const& releases = manager.GetAllReleases(); !releases.empty())
    {
        auto const latestApplied = ComputeLatestAppliedRelease(manager);
        if (!latestApplied.latest)
        {
            std::println("  {:<{}}(none applied)", "Latest applied release:", labelWidth);
        }
        else
        {
            auto const* const label = (latestApplied.applied == latestApplied.total) ? "applied" : "partially applied";
            std::println("  {:<{}}{} ({}, {}/{} migrations)",
                         "Latest applied release:",
                         labelWidth,
                         latestApplied.latest->version,
                         label,
                         latestApplied.applied,
                         latestApplied.total);
        }

        auto const& latestAvailable = releases.back();
        std::println("  {:<{}}{}", "Latest available release:", labelWidth, latestAvailable.version);
    }

    std::println("");

    if (!mismatches.empty())
    {
        std::println("Checksum Mismatches ({}):", mismatches.size());
        for (auto const& result: mismatches)
        {
            std::println("  {} - {}", result.timestamp.value, result.title);
            std::println("      Stored:   {}", result.storedChecksum.empty() ? "(none)" : result.storedChecksum);
            std::println("      Computed: {}",
                         result.computedChecksum.empty() ? "(migration not found)" : result.computedChecksum);
        }
        std::println("");
        std::println(
            "WARNING: {} migration(s) have checksum mismatches. The migration code may have changed after application.",
            mismatches.size());
    }
    else
    {
        std::println("All applied migrations have valid checksums.");
    }

    return EXIT_SUCCESS;
}

/// Whether the user confirmed a destructive admin command up front.
enum class Confirmation : std::uint8_t
{
    Ask,   ///< Not confirmed: the command refuses and says how to confirm.
    Given, ///< Confirmed with `--yes`.
};

/// @return `DryRun` when the command only previews, else `Execute`.
ExecutionMode ExecutionModeOf(Options const& options)
{
    return options.dryRun ? ExecutionMode::DryRun : ExecutionMode::Execute;
}

/// @return `Given` when `--yes` was passed, else `Ask`.
Confirmation ConfirmationOf(Options const& options)
{
    return options.yes ? Confirmation::Given : Confirmation::Ask;
}

/// @brief Generic "render plan → if empty, exit success → print diff → if dry-run,
/// exit success → if not `--yes`, refuse → execute → print summary" loop, factored
/// out so `rewrite-checksums`, `hard-reset`, and `unicode-upgrade-tables` share
/// the same UX without duplicating it.
///
/// The caller supplies the four template hooks via lambdas — `run` is invoked
/// twice (once with `ExecutionMode::DryRun`, once with `ExecutionMode::Execute`) so the
/// manager-level API doesn't have to memoize results across calls.
template <typename Result>
int RunAdminCommand(std::string_view name,
                    ExecutionMode mode,
                    Confirmation confirmation,
                    auto&& runFn,
                    auto&& isEmptyFn,
                    auto&& printDiffFn,
                    auto&& printSummaryFn)
{
    auto const preview = runFn(ExecutionMode::DryRun);

    if (isEmptyFn(preview))
    {
        std::println("No {} changes needed. Nothing to do.", name);
        return EXIT_SUCCESS;
    }

    printDiffFn(preview);

    if (mode == ExecutionMode::DryRun)
    {
        std::println("Dry run: nothing written. Re-run without --dry-run to apply.");
        return EXIT_SUCCESS;
    }

    if (confirmation == Confirmation::Ask)
    {
        std::println("Refusing to {} without --yes. Pass --yes to confirm.", name);
        return EXIT_FAILURE;
    }

    auto const written = runFn(ExecutionMode::Execute);
    printSummaryFn(written);
    return EXIT_SUCCESS;
}

/// Rewrites stored checksums in `schema_migrations` to match the current code.
///
/// Used after a regen of generated migrations changes byte shape but not logical
/// effect. The caller is expected to have verified out-of-band that the regen
/// is logically equivalent — this command is a recovery tool, not a maintenance
/// loop.
///
/// In dry-run mode the diff is printed and nothing is written. Without dry-run the
/// caller must explicitly confirm; otherwise the command exits without writing.
int RewriteChecksums(MigrationManager& manager, ExecutionMode mode, Confirmation confirmation)
{
    return RunAdminCommand<MigrationManager::RewriteChecksumsResult>(
        "rewrite-checksums",
        mode,
        confirmation,
        [&](ExecutionMode runMode) { return manager.RewriteChecksums(runMode == ExecutionMode::DryRun); },
        [](auto const& r) { return r.entries.empty() && r.unregisteredTimestamps.empty(); },
        [](auto const& r) {
            if (!r.entries.empty())
            {
                std::println("Checksum drift ({} migration(s)):", r.entries.size());
                for (auto const& entry: r.entries)
                {
                    std::println("  {} - {}", entry.timestamp.value, entry.title);
                    std::println("      Stored:   {}", entry.oldChecksum.empty() ? "(none)" : entry.oldChecksum);
                    std::println("      Computed: {}", entry.newChecksum);
                }
                std::println("");
            }
            if (!r.unregisteredTimestamps.empty())
            {
                std::println("Applied but unregistered ({}):", r.unregisteredTimestamps.size());
                for (auto const& ts: r.unregisteredTimestamps)
                    std::println("  {}", ts.value);
                std::println("  (these rows are NOT touched by rewrite-checksums)");
                std::println("");
            }
        },
        [](auto const& r) { std::println("Rewrote {} checksum(s).", r.entries.size()); });
}

/// @brief Drops every migration-owned table, preserves user tables.
int HardReset(MigrationManager& manager, ExecutionMode mode, Confirmation confirmation)
{
    return RunAdminCommand<MigrationManager::HardResetResult>(
        "hard-reset",
        mode,
        confirmation,
        [&](ExecutionMode runMode) { return manager.HardReset(runMode); },
        [](auto const& r) { return r.droppedTables.empty() && r.absentTables.empty() && r.preservedTables.empty(); },
        [](auto const& r) {
            if (!r.droppedTables.empty())
            {
                std::println("Tables to drop ({}):", r.droppedTables.size());
                for (auto const& t: r.droppedTables)
                    std::println("  {}", t.table);
            }
            if (!r.absentTables.empty())
            {
                std::println("Migration-declared but absent ({}): no-op", r.absentTables.size());
                for (auto const& t: r.absentTables)
                    std::println("  {}", t.table);
            }
            if (!r.preservedTables.empty())
            {
                std::println("");
                std::println("USER-OWNED tables (preserved, NOT dropped) ({}):", r.preservedTables.size());
                for (auto const& t: r.preservedTables)
                    std::println("  {}", t.table);
                std::println("");
            }
        },
        [](auto const& r) {
            std::println("Dropped {} table(s){}.",
                         r.droppedTables.size(),
                         r.schemaMigrationsDropped ? " plus schema_migrations" : "");
        });
}

/// @brief Upgrades legacy VARCHAR/CHAR columns to NVARCHAR/NCHAR.
int UnicodeUpgradeTables(MigrationManager& manager, Options const& options)
{
    return RunAdminCommand<MigrationManager::UnicodeUpgradeResult>(
        "unicode-upgrade-tables",
        ExecutionModeOf(options),
        ConfirmationOf(options),
        [&](ExecutionMode mode) {
            if (mode == ExecutionMode::DryRun)
                return manager.UnicodeUpgradeTables(ExecutionMode::DryRun);
            auto progress = CommandProgress { options };
            auto result = [&] {
                auto const sink = progress.RoutesMessagesThroughTask()
                                      ? std::make_unique<ScopedProgressLogSink>(manager, progress.Task())
                                      : nullptr;
                return manager.UnicodeUpgradeTables(ExecutionMode::Execute, &progress.Task());
            }();
            progress.Finish();
            return result;
        },
        [](auto const& r) { return r.columns.empty(); },
        [](auto const& r) { Tools::PrintUnicodeUpgradePlan(std::cout, r); },
        [](auto const& r) {
            std::set<std::string> tables;
            for (auto const& c: r.columns)
                tables.insert(c.table.table);
            std::println("Upgraded {} column(s) across {} table(s).", r.columns.size(), tables.size());
            std::println("Committed {} transaction(s), one per unit.", r.units.size());
        });
}

/// Marks a migration as applied without executing its Up() method.
///
/// @param manager The migration manager instance.
/// @param argument The migration timestamp to mark as applied.
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
int MarkApplied(MigrationManager& manager, std::string_view argument)
{
    auto const markApplied = [&manager](MigrationBase const* migration) -> std::expected<int, std::string> {
        try
        {
            manager.MarkMigrationAsApplied(*migration);
            std::println("Marked migration {} - {} as applied.", migration->GetTimestamp().value, migration->GetTitle());
            return EXIT_SUCCESS;
        }
        catch (std::exception const& e)
        {
            return std::unexpected(e.what());
        }
    };
    return ExitCodeOf(GetMigration(manager, argument).and_then(markApplied));
}

/// Rolls back all migrations applied after the specified timestamp.
///
/// @param manager The migration manager instance.
/// @param argument The target migration timestamp.
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
int RollbackTo(MigrationManager& manager, std::string_view argument)
{
    if (argument.empty())
    {
        std::println(std::cerr, "Error: Target migration timestamp is required.");
        return EXIT_FAILURE;
    }

    auto targetTs = uint64_t {};
    auto const res = std::from_chars(argument.data(), argument.data() + argument.size(), targetTs);
    if (res.ec != std::errc {})
    {
        std::println(std::cerr, "Error: Migration timestamp is invalid.");
        return EXIT_FAILURE;
    }

    auto const targetTimestamp = MigrationTimestamp { targetTs };

    // Verify the target migration exists
    if (!manager.GetMigration(targetTimestamp))
    {
        std::println(std::cerr, "Error: Target migration {} not found.", targetTs);
        return EXIT_FAILURE;
    }

    std::println("Rolling back migrations after timestamp {}...", targetTs);
    std::println("");

    auto const skippedBefore = manager.SkippedExistingRowCount();
    auto const result = manager.RevertToMigration(targetTimestamp, [](MigrationBase const& m, size_t i, size_t n) {
        std::println("[{}/{}] Rolling back {} - {}", i + 1, n, m.GetTimestamp().value, m.GetTitle());
    });

    if (result.revertedTimestamps.empty() && !result.failedAt.has_value())
    {
        std::println("No migrations to rollback. Database is already at or before timestamp {}.", targetTs);
        return EXIT_SUCCESS;
    }

    if (result.failedAt.has_value())
    {
        std::println(std::cerr, "");
        std::println(std::cerr, "Error: failed to rollback migration.");
        std::println(std::cerr, "  Migration:    {} - {}", result.failedAt->value, result.failedTitle);
        if (!result.failedSql.empty())
        {
            std::println(std::cerr, "  Step:         {}", result.failedStepIndex);
            if (!result.sqlState.empty() && result.sqlState != "     ")
                std::println(std::cerr, "  SQL State:    {}, Native error: {}", result.sqlState, result.nativeErrorCode);
            std::println(std::cerr, "  Driver message:");
            std::println(std::cerr, "    {}", result.errorMessage);
            std::println(std::cerr, "  Failed SQL:");
            std::println(std::cerr, "    {}", result.failedSql);
        }
        else
        {
            std::println(std::cerr, "  Message:      {}", result.errorMessage);
        }
        std::println(std::cerr,
                     "Rollback stopped. {} migration(s) were rolled back before the failure.",
                     result.revertedTimestamps.size());
        return EXIT_FAILURE;
    }

    std::println("");
    std::println("Successfully rolled back {} migration(s){}.",
                 result.revertedTimestamps.size(),
                 SkippedRowsSuffix(manager, skippedBefore));
    return EXIT_SUCCESS;
}

/// Lists all declared software releases with their highest-migration timestamps
/// and how many registered migrations fall in each release's range.
///
/// @param manager The migration manager instance.
/// @return EXIT_SUCCESS.
int Releases(MigrationManager& manager)
{
    auto const& releases = manager.GetAllReleases();

    if (releases.empty())
    {
        std::println("No releases declared.");
        return EXIT_SUCCESS;
    }

    // Materialize applied IDs into a hash set so the per-release all_of/any_of checks below
    // are O(1) per lookup instead of a linear scan over `applied`.
    auto const applied = manager.GetAppliedMigrationIds();
    std::unordered_set<uint64_t> appliedSet;
    appliedSet.reserve(applied.size());
    for (auto const& ts: applied)
        appliedSet.insert(ts.value);
    auto const isApplied = [&](MigrationTimestamp ts) {
        return appliedSet.contains(ts.value);
    };

    std::println("Releases:");
    std::println("");
    std::println("  {:<16} {:<24} {:>10}  {}", "Version", "Highest Timestamp", "Migrations", "Status");

    for (auto const& release: releases)
    {
        auto const migrations = manager.GetMigrationsForRelease(release.version);

        // A release is "fully applied" iff every migration in its range is applied.
        // If it has no migrations, treat as "empty" rather than applied.
        auto const allApplied =
            !migrations.empty()
            && std::ranges::all_of(migrations, [&](MigrationBase const* m) { return isApplied(m->GetTimestamp()); });
        auto const anyApplied =
            std::ranges::any_of(migrations, [&](MigrationBase const* m) { return isApplied(m->GetTimestamp()); });

        std::string_view status;
        if (migrations.empty())
            status = "empty";
        else if (allApplied)
            status = "applied";
        else if (anyApplied)
            status = "partial";
        else
            status = "pending";

        std::println(
            "  {:<16} {:<24} {:>10}  {}", release.version, release.highestTimestamp.value, migrations.size(), status);
    }

    return EXIT_SUCCESS;
}

/// Applies all pending migrations whose timestamp is `<= highestTimestamp` of the named release.
///
/// Forward-only counterpart of `RollbackToRelease`: if the database is already at or past the
/// target release, this is a no-op. To go *back* to an earlier release, use `rollback-to-release`.
///
/// @param manager The migration manager instance.
/// @param argument The target release version string.
/// @param mode With `DryRun`, print the SQL that would be executed without touching the database.
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
int MigrateToRelease(MigrationManager& manager,
                     CommandProgress const& progress,
                     std::string_view argument,
                     ExecutionMode mode)
{
    if (argument.empty())
    {
        std::println(std::cerr, "Error: Target release version is required.");
        return EXIT_FAILURE;
    }

    auto const* release = manager.FindReleaseByVersion(argument);
    if (!release)
    {
        std::println(std::cerr, "Error: Release '{}' is not declared.", argument);
        return EXIT_FAILURE;
    }

    // Forward-only: refuse to silently revert if the user is already past the target.
    auto const applied = manager.GetAppliedMigrationIds();
    auto const highestApplied = std::ranges::max_element(applied, {}, [](MigrationTimestamp ts) { return ts.value; });
    if (highestApplied != applied.end() && highestApplied->value >= release->highestTimestamp.value)
    {
        std::println("Database is already at or past release '{}' (highest applied: {}, release boundary: {}).",
                     argument,
                     highestApplied->value,
                     release->highestTimestamp.value);
        std::println("Use `dbtool rollback-to-release {}` to go back.", argument);
        return EXIT_SUCCESS;
    }

    if (!RunPreMigrateStepsOf(manager, progress, mode))
        return EXIT_FAILURE;

    if (mode == ExecutionMode::DryRun)
    {
        std::println("-- Dry run: SQL that would be executed up to release '{}' (timestamp {})\n",
                     release->version,
                     release->highestTimestamp.value);
        auto statements =
            manager.PreviewPendingMigrationsUpTo(release->highestTimestamp, [](MigrationBase const& m, size_t i, size_t n) {
                std::println("-- [{}/{}] Migration: {} - {}", i + 1, n, m.GetTimestamp().value, m.GetTitle());
            });

        if (statements.empty())
        {
            std::println("-- No pending migrations up to release '{}'.", argument);
        }
        else
        {
            for (auto const& sql: statements)
                std::println("{};", sql);
            std::println("\n-- Total: {} SQL statements", statements.size());
        }
        return EXIT_SUCCESS;
    }

    std::println("Applying pending migrations up to release '{}' (timestamp {})...",
                 release->version,
                 release->highestTimestamp.value);
    auto const skippedBefore = manager.SkippedExistingRowCount();
    auto phase = MigrationsPhase { manager, progress };
    size_t const count = phase.Run([&] {
        return manager.ApplyPendingMigrationsUpTo(
            release->highestTimestamp, [&phase](MigrationBase const& m, size_t i, size_t n) { phase.Starting(m, i, n); });
    });
    if (count > 0)
        phase.Done();
    phase.Close();
    std::println(
        "Applied {} migration(s) up to release '{}'{}.", count, argument, SkippedRowsSuffix(manager, skippedBefore));
    return EXIT_SUCCESS;
}

/// Rolls back all migrations applied after the release identified by `version`.
///
/// Resolves `version` to its `highestTimestamp` and delegates to `RevertToMigration`. Migrations
/// whose timestamp is `<= highestTimestamp` are kept; those strictly greater are reverted in
/// reverse timestamp order.
///
/// @param manager The migration manager instance.
/// @param argument The target release version string.
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
int RollbackToRelease(MigrationManager& manager, std::string_view argument)
{
    if (argument.empty())
    {
        std::println(std::cerr, "Error: Target release version is required.");
        return EXIT_FAILURE;
    }

    auto const* release = manager.FindReleaseByVersion(argument);
    if (!release)
    {
        std::println(std::cerr, "Error: Release '{}' is not declared.", argument);
        return EXIT_FAILURE;
    }

    std::println(
        "Rolling back migrations after release '{}' (timestamp {})...", release->version, release->highestTimestamp.value);
    std::println("");

    auto const skippedBefore = manager.SkippedExistingRowCount();
    auto const result = manager.RevertToMigration(release->highestTimestamp, [](MigrationBase const& m, size_t i, size_t n) {
        std::println("[{}/{}] Rolling back {} - {}", i + 1, n, m.GetTimestamp().value, m.GetTitle());
    });

    if (result.revertedTimestamps.empty() && !result.failedAt.has_value())
    {
        std::println("No migrations to rollback. Database is already at or before release '{}'.", argument);
        return EXIT_SUCCESS;
    }

    if (result.failedAt.has_value())
    {
        std::println(std::cerr, "");
        std::println(std::cerr, "Error: failed to rollback migration.");
        std::println(std::cerr, "  Migration:    {} - {}", result.failedAt->value, result.failedTitle);
        if (!result.failedSql.empty())
        {
            std::println(std::cerr, "  Step:         {}", result.failedStepIndex);
            if (!result.sqlState.empty() && result.sqlState != "     ")
                std::println(std::cerr, "  SQL State:    {}, Native error: {}", result.sqlState, result.nativeErrorCode);
            std::println(std::cerr, "  Driver message:");
            std::println(std::cerr, "    {}", result.errorMessage);
            std::println(std::cerr, "  Failed SQL:");
            std::println(std::cerr, "    {}", result.failedSql);
        }
        else
        {
            std::println(std::cerr, "  Message:      {}", result.errorMessage);
        }
        std::println(std::cerr,
                     "Rollback stopped. {} migration(s) were rolled back before the failure.",
                     result.revertedTimestamps.size());
        return EXIT_FAILURE;
    }

    std::println("");
    std::println("Successfully rolled back {} migration(s) to release '{}'{}.",
                 result.revertedTimestamps.size(),
                 argument,
                 SkippedRowsSuffix(manager, skippedBefore));
    return EXIT_SUCCESS;
}

/// RAII wrapper for optional advisory locking around migration commands.
///
/// Acquires the migration-system advisory lock if locking is enabled,
/// otherwise does nothing. The lock is automatically released when the
/// wrapper goes out of scope. Wraps `Lightweight::SqlScopedLock` —
/// callers outside dbtool should use `SqlScopedLock` directly.
class OptionalScopedLock
{
  public:
    OptionalScopedLock(MigrationManager& manager, bool noLock):
        _lock(noLock ? std::nullopt
                     : std::make_optional<SqlScopedLock>(manager.GetDataMapper().Connection(), Tools::MigrationLockName))
    {
        if (_lock && !_lock->IsLocked())
        {
            throw std::runtime_error("Failed to acquire migration lock. Another migration may be in progress.");
        }
    }

  private:
    std::optional<SqlScopedLock> _lock;
};

class SimpleEventProgressManager: public Lightweight::SqlBackup::ErrorTrackingProgressManager
{
  public:
    void Update(SqlBackup::Progress const& p) override
    {
        ErrorTrackingProgressManager::Update(p);

        std::string_view stateStr;
        switch (p.state)
        {
            using enum SqlBackup::Progress::State;
            case Started:
                stateStr = "Started";
                break;
            case InProgress:
                stateStr = "InProgress";
                break;
            case Finished:
                stateStr = "Finished";
                break;
            case Error:
                stateStr = "Error";
                break;
            case Warning:
                stateStr = "Warning";
                break;
        }

        if (p.totalRows && *p.totalRows > 0)
        {
            auto const pct = (static_cast<double>(p.currentRows) * 100.0) / static_cast<double>(*p.totalRows);
            std::println("[{}] {} ({:.2f}%) {}", p.tableName, stateStr, pct, p.message);
        }
        else
        {
            std::println("[{}] {} {} rows {}", p.tableName, stateStr, p.currentRows, p.message);
        }

        if (p.state == SqlBackup::Progress::State::Warning)
        {
            _issuesByTable[p.tableName].push_back({ .message = p.message, .type = Tools::IssueType::Warning });
        }
        if (p.state == SqlBackup::Progress::State::Error)
        {
            _issuesByTable[p.tableName].push_back({ .message = p.message, .type = Tools::IssueType::Error });
        }
    }

    void AllDone() override
    {
        auto const now = std::chrono::steady_clock::now();
        auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _startTime);
        auto const total_ms = elapsed.count();
        auto const h = total_ms / 3600000;
        auto const m = (total_ms % 3600000) / 60000;
        auto const s = (total_ms % 60000) / 1000;
        auto const ms = total_ms % 1000;
        std::println("Total time: {:02}:{:02}:{:02}.{:03}", h, m, s, ms);

        if (!_issuesByTable.empty())
        {
            std::println("\nIssues:");
            for (auto const& [tableName, issues]: _issuesByTable)
            {
                std::println("  {}:", tableName);
                for (auto const& issue: issues)
                {
                    switch (issue.type)
                    {
                        using enum Tools::IssueType;
                        case Error:
                            std::println("    ❌ {}", issue.message);
                            break;
                        case Warning:
                            std::println("    ⚠️  {}", issue.message);
                            break;
                        case Info:
                            std::println("    ℹ️  {}", issue.message);
                            break;
                    }
                }
            }
        }
    }

  private:
    std::map<std::string, std::vector<Tools::TableIssue>> _issuesByTable;
    std::chrono::steady_clock::time_point _startTime = std::chrono::steady_clock::now();
};

std::unique_ptr<Lightweight::SqlBackup::ProgressManager> CreateProgressManager(Options const& options)
{
    switch (options.progressType)
    {
        case ProgressType::Unicode:
            return std::make_unique<Tools::StandardProgressManager>(true);
        case ProgressType::Ascii:
            return std::make_unique<Tools::StandardProgressManager>(false);
        case ProgressType::Loglines:
            return std::make_unique<SimpleEventProgressManager>();
        case ProgressType::None:
            return std::make_unique<Lightweight::SqlBackup::NullProgressManager>();
    }
    std::unreachable();
}

/// Parses a size string with optional K/KB/M/MB/G/GB suffix to bytes.
///
/// @param sizeStr The size string (e.g., "10M", "1GB", "1024").
/// @return The size in bytes, or an error message.
std::expected<std::size_t, std::string> ParseSizeWithSuffix(std::string_view sizeStr)
{
    if (sizeStr.empty())
        return std::unexpected { "Size cannot be empty" };

    // Find where the numeric part ends
    size_t numEnd = 0;
    while (numEnd < sizeStr.size() && (std::isdigit(sizeStr[numEnd]) || sizeStr[numEnd] == '.'))
        ++numEnd;

    if (numEnd == 0)
        return std::unexpected { std::format("Invalid size '{}': must start with a number", sizeStr) };

    // Locale-independent, error-reporting float parse (std::from_chars' float overloads are unavailable
    // before macOS 26 in libc++); see Lightweight::detail::ParseFloat.
    auto const numPart = sizeStr.substr(0, numEnd);
    auto const parsed = Lightweight::detail::ParseFloat<double>(numPart.data(), numPart.data() + numPart.size());
    if (!parsed)
        return std::unexpected { std::format("Invalid size '{}': invalid number", sizeStr) };
    double const value = *parsed;

    // Parse suffix
    std::string suffix(sizeStr.substr(numEnd));
    std::ranges::transform(suffix, suffix.begin(), [](unsigned char c) { return std::toupper(c); });

    std::size_t multiplier = 1;
    if (suffix.empty() || suffix == "B")
        multiplier = 1;
    else if (suffix == "K" || suffix == "KB")
        multiplier = 1024;
    else if (suffix == "M" || suffix == "MB")
        multiplier = 1024 * 1024;
    else if (suffix == "G" || suffix == "GB")
        multiplier = 1024 * 1024 * 1024;
    else
        return std::unexpected { std::format("Unknown size suffix '{}'. Use: K, KB, M, MB, G, GB", suffix) };

    return static_cast<std::size_t>(value * static_cast<double>(multiplier));
}

/// Parses backup settings from CLI options.
///
/// @param options The CLI options containing compression method, level, and chunk size.
/// @return The parsed BackupSettings, or an error message.
std::expected<Lightweight::SqlBackup::BackupSettings, std::string> ParseBackupSettings(Options const& options)
{
    using namespace Lightweight::SqlBackup;
    BackupSettings settings;
    settings.level = options.compressionLevel;

    // Parse compression method string
    std::string method = options.compressionMethod;
    std::ranges::transform(method, method.begin(), [](unsigned char c) { return std::tolower(c); });

    if (method == "none" || method == "store")
        settings.method = CompressionMethod::Store;
    else if (method == "deflate")
        settings.method = CompressionMethod::Deflate;
    else if (method == "bzip2")
        settings.method = CompressionMethod::Bzip2;
    else if (method == "lzma")
        settings.method = CompressionMethod::Lzma;
    else if (method == "zstd")
        settings.method = CompressionMethod::Zstd;
    else if (method == "xz")
        settings.method = CompressionMethod::Xz;
    else
        return std::unexpected { std::format("Unknown compression method '{}'. Use: none, deflate, bzip2, lzma, zstd, xz",
                                             options.compressionMethod) };

    // Check if the method is supported by the current libzip installation
    if (!IsCompressionMethodSupported(settings.method))
    {
        auto supported = GetSupportedCompressionMethods();
        std::string supportedList;
        for (size_t i = 0; i < supported.size(); ++i)
        {
            if (i > 0)
                supportedList += ", ";
            supportedList += CompressionMethodName(supported[i]);
        }
        return std::unexpected { std::format("Compression method '{}' is not supported by your libzip installation. "
                                             "Supported methods: {}",
                                             options.compressionMethod,
                                             supportedList) };
    }

    // Validate compression level
    if (options.compressionLevel > 9)
        return std::unexpected { "Compression level must be between 0 and 9" };

    // Parse chunk size
    auto chunkSizeResult = ParseSizeWithSuffix(options.chunkSize);
    if (!chunkSizeResult)
        return std::unexpected { std::format("Invalid chunk size: {}", chunkSizeResult.error()) };
    settings.chunkSizeBytes = chunkSizeResult.value();

    // Validate minimum chunk size (at least 1 KB)
    if (settings.chunkSizeBytes < 1024)
        return std::unexpected { "Chunk size must be at least 1 KB" };

    settings.schemaOnly = options.schemaOnly;

    // No longer has any effect (the MSSQL concurrency clamp was removed); kept until the field is deleted.
    settings.forceMssqlConcurrency = std::getenv("LIGHTWEIGHT_BACKUP_FORCE_MSSQL_CONCURRENCY") != nullptr;

    return settings;
}

/// Formats connection error messages for the user.
///
/// @param errorMessage The raw error message from the database driver.
/// @return A user-friendly formatted error message.
std::string FormatConnectionError(std::string_view errorMessage)
{
    // Check for common connection error patterns and provide helpful suggestions
    std::string const lowerMsg = [&] {
        std::string s(errorMessage);
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }();

    std::string suggestions;
    if (lowerMsg.contains("login failed") || lowerMsg.contains("authentication"))
        suggestions = "\n  - Check that the username and password are correct";
    else if (lowerMsg.contains("timeout") || lowerMsg.contains("timed out"))
        suggestions = "\n  - Check that the server is reachable and not overloaded"
                      "\n  - Verify the network connection";
    else if (lowerMsg.contains("server") && (lowerMsg.contains("not found") || lowerMsg.contains("unknown")))
        suggestions = "\n  - Verify the server address in the connection string"
                      "\n  - Check that the database server is running";
    else if (lowerMsg.contains("driver") && lowerMsg.contains("not found"))
        suggestions = "\n  - Install the required ODBC driver"
                      "\n  - Verify the DRIVER parameter in the connection string";
    else if (lowerMsg.contains("tcp provider") || lowerMsg.contains("network") || lowerMsg.contains("connection refused"))
        suggestions = "\n  - Check that the server address and port are correct"
                      "\n  - Verify the server is accepting connections"
                      "\n  - Check firewall settings";
    else if (lowerMsg.contains("database") && lowerMsg.contains("not exist"))
        suggestions = "\n  - Verify the DATABASE parameter in the connection string"
                      "\n  - Check that the database exists on the server";

    return std::format("{}{}", errorMessage, suggestions);
}

/// Splits a comma-separated list, dropping empty items and surrounding spaces.
std::vector<std::string> SplitCommaList(std::string_view list)
{
    std::vector<std::string> items;
    for (auto const item: std::views::split(list, ','))
    {
        auto text = std::string_view { item.begin(), item.end() };
        auto const first = text.find_first_not_of(' ');
        if (first == std::string_view::npos)
            continue;
        text = text.substr(first, text.find_last_not_of(' ') - first + 1);
        items.emplace_back(text);
    }
    return items;
}

/// Resolves the schemas `backup` covers: every user schema for `--all-schemas`, else `--schema`.
///
/// @return The schemas (one possibly empty name for a single-schema backup), or an error message.
std::expected<std::vector<std::string>, std::string> ResolveBackupSchemas(Options const& options)
{
    if (!options.allSchemas)
    {
        auto schemas = SplitCommaList(options.schema);
        if (schemas.empty())
            schemas.emplace_back();
        return schemas;
    }

    if (options.schemaSet)
        return std::unexpected { "--all-schemas and --schema exclude each other" };

    SqlConnection conn { std::nullopt };
    if (!conn.Connect(options.connectionString))
        return std::unexpected { FormatConnectionError(
            std::format("Failed to connect to database: {}", conn.LastError().message)) };
    auto stmt = SqlStatement { conn };
    auto schemas = SqlSchema::AllUserSchemas(stmt, conn.DatabaseName());
    if (schemas.empty())
        return std::unexpected { "--all-schemas: the database has no schema that holds a user table" };
    return schemas;
}

/// Shows what `backup` would write, without writing it.
///
/// @param options The CLI options.
/// @param requestedSchemas The schemas to back up (see `ResolveBackupSchemas`); duplicates count once.
/// @param outputFile The archive that would be written.
/// @return The process exit code.
int BackupDryRun(Options const& options,
                 std::vector<std::string> const& requestedSchemas,
                 std::filesystem::path const& outputFile)
{
    SqlConnection conn { std::nullopt };
    if (!conn.Connect(options.connectionString))
    {
        std::println(std::cerr,
                     "Error: {}",
                     FormatConnectionError(std::format("Failed to connect to database: {}", conn.LastError().message)));
        return EXIT_FAILURE;
    }
    auto const schemas = Lightweight::SqlBackup::detail::NormalizeSchemaNames(
        requestedSchemas, conn.QueryFormatter().IdentifiersAreCaseInsensitive());
    bool const multiSchema = schemas.size() > 1;

    auto pm = CreateProgressManager(options);

    // Parse filter early to use as predicate during schema scanning
    auto const filter = Lightweight::SqlBackup::TableFilter::Parse(options.filterTables);

    SqlStatement stmt { conn };
    SqlSchema::ReadAllTablesCallback schemaCallback =
        [&pm](std::string_view tableName, size_t const current, size_t const total) {
            pm->Update({ .state = SqlBackup::Progress::State::InProgress,
                         .tableName = "Scanning schema",
                         .currentRows = current,
                         .totalRows = total,
                         .message = std::format("Scanning table {}", tableName) });
        };
    SqlSchema::TableList tables;
    for (auto const& schema: schemas)
    {
        // Skips reading the schema of tables the filter rejects.
        SqlSchema::TableFilterPredicate tableFilterPredicate;
        if (!filter.MatchesAll())
        {
            tableFilterPredicate = [&filter, &schema](std::string_view /*schemaName*/, std::string_view tableName) {
                return filter.Matches(schema, tableName);
            };
        }
        std::ranges::move(
            SqlSchema::ReadAllTables(stmt, conn.DatabaseName(), schema, schemaCallback, {}, tableFilterPredicate),
            std::back_inserter(tables));
    }

    pm->Update({ .state = SqlBackup::Progress::State::Finished,
                 .tableName = "Scanning schema",
                 .currentRows = tables.size(),
                 .totalRows = tables.size(),
                 .message = "" });

    pm->AllDone();

    std::println("");
    if (options.schemaOnly)
        std::println("Dry run: Would backup schema only for {} tables to {}", tables.size(), outputFile.string());
    else
        std::println("Dry run: Would backup {} tables to {}", tables.size(), outputFile.string());
    std::println("");

    size_t totalRows = 0;
    for (auto const& table: tables)
    {
        auto const tableName =
            Lightweight::SqlBackup::detail::FormatTableName(multiSchema ? table.schema : std::string {}, table.name);
        auto rowCount = stmt.ExecuteDirectScalar<int64_t>(std::format("SELECT COUNT(*) FROM {}", tableName)).value_or(0);
        totalRows += static_cast<size_t>(rowCount);
        std::println("  {} ({} rows, {} columns)",
                     multiSchema ? std::format("{}.{}", table.schema, table.name) : table.name,
                     rowCount,
                     table.columns.size());
    }
    std::println("");
    std::println("Total: {} tables, {} rows", tables.size(), totalRows);

    if (auto const collisions = Lightweight::SqlBackup::detail::DescribeTableNameCollisions(tables, multiSchema);
        !collisions.empty())
    {
        std::println(std::cerr, "Error: the backup would fail: {}", collisions);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int Backup(Options const& options)
{
    namespace Cfg = Lightweight::Config;

    // `--output` as given, else (or for a bare file name) the folder dbtool.yml names.
    auto const output = Cfg::ResolveBackupOutput(
        { .requested = options.outputFile,
          .folder = options.backupDir,
          .profileName = options.selectedProfile ? std::string_view { options.selectedProfile->name } : std::string_view {},
          .now = std::chrono::system_clock::now() });
    if (!output)
    {
        std::println(std::cerr,
                     "Error: --output file required for backup (or set `defaultBackupDir` / the profile's `backupDir` "
                     "in dbtool.yml).");
        return EXIT_FAILURE;
    }
    auto const& outputFile = output->path;
    if (output->inConfiguredFolder)
    {
        std::error_code ec;
        std::filesystem::create_directories(outputFile.parent_path(), ec);
        if (ec)
        {
            std::println(
                std::cerr, "Error: cannot create backup folder {}: {}", outputFile.parent_path().string(), ec.message());
            return EXIT_FAILURE;
        }
        std::println("Backup file: {}", outputFile.string());
    }

    // Parse and validate backup settings (compression + chunk size)
    auto backupSettingsResult = ParseBackupSettings(options);
    if (!backupSettingsResult)
    {
        std::println(std::cerr, "Error: {}", backupSettingsResult.error());
        return EXIT_FAILURE;
    }
    auto const& backupSettings = backupSettingsResult.value();

    auto const schemasResult = ResolveBackupSchemas(options);
    if (!schemasResult)
    {
        std::println(std::cerr, "Error: {}", schemasResult.error());
        return EXIT_FAILURE;
    }
    auto const& schemas = *schemasResult;

    if (options.dryRun)
        return BackupDryRun(options, schemas, outputFile);

    auto pm = CreateProgressManager(options);
    Lightweight::SqlBackup::RetrySettings retrySettings { .maxRetries = options.maxRetries };

    try
    {
        Lightweight::SqlBackup::BackupSchemas(outputFile,
                                              options.connectionString,
                                              options.jobs,
                                              *pm,
                                              schemas,
                                              options.filterTables,
                                              retrySettings,
                                              backupSettings);
    }
    catch (SqlException const& e)
    {
        // SQL-specific exception - format the connection error nicely
        std::println(std::cerr, "Error: {}", FormatConnectionError(e.info().message));
        return EXIT_FAILURE;
    }
    catch (std::runtime_error const& e)
    {
        std::string_view const msg = e.what();
        if (msg.starts_with("Failed to connect"))
            std::println(std::cerr, "Error: {}", FormatConnectionError(msg));
        else
            std::println(std::cerr, "Error: {}", msg);
        return EXIT_FAILURE;
    }

    return pm->ErrorCount() > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

/// Parses restore settings from CLI options.
///
/// @param options The CLI options.
/// @return RestoreSettings or an error message.
std::expected<Lightweight::SqlBackup::RestoreSettings, std::string> ParseRestoreSettings(Options const& options)
{
    Lightweight::SqlBackup::RestoreSettings settings;

    if (!options.memoryLimit.empty())
    {
        auto memoryResult = ParseSizeWithSuffix(options.memoryLimit);
        if (!memoryResult)
            return std::unexpected { std::format("Invalid memory limit: {}", memoryResult.error()) };
        settings.memoryLimitBytes = memoryResult.value();
    }

    if (!options.batchSize.empty())
    {
        try
        {
            settings.batchSize = std::stoull(options.batchSize);
        }
        catch (std::exception const&)
        {
            return std::unexpected { std::format("Invalid batch size: {}", options.batchSize) };
        }
    }

    settings.schemaOnly = options.schemaOnly;

    return settings;
}

/// Decides the schema `restore` targets.
///
/// A multi-schema archive restores each table into its own schema. It refuses an explicit
/// `--schema`, and a profile's schema unless that names one of the archive's schemas.
///
/// @return The target schema (empty for the archive's own), or an error message.
std::expected<std::string, std::string> ResolveRestoreSchema(Options const& options)
{
    if (options.allSchemas)
        return std::unexpected { "--all-schemas applies to backup only" };
    if (options.schemaSet && options.schema.contains(','))
        return std::unexpected { "restore takes a single --schema" };

    auto const archiveSchemas = Lightweight::SqlBackup::ReadArchiveSchemas(options.inputFile);
    if (!archiveSchemas || archiveSchemas->size() <= 1)
        return options.schema; // a single-schema archive, or one the restore itself reports on

    auto const targetIsCaseInsensitive = [&options] {
        TraceBreadcrumb("ResolveRestoreSchema: probing the target's identifier case");
        SqlConnection probe { std::nullopt };
        return probe.Connect(options.connectionString) && probe.QueryFormatter().IdentifiersAreCaseInsensitive();
    };
    auto const schema =
        options.schemaSet || options.schema.empty()
            ? options.schema
            : Lightweight::SqlBackup::DefaultRestoreSchema(options.inputFile, options.schema, targetIsCaseInsensitive());
    if (schema.empty())
        return std::string {};
    return std::unexpected { std::format(
        "{} cannot be used with this archive: it holds several schemas ({}), and each table is restored into the "
        "schema it comes from",
        options.schemaSet ? std::string { "--schema" } : std::format("the profile's schema '{}'", schema),
        Lightweight::SqlBackup::detail::JoinNames(*archiveSchemas)) };
}

/// Shows what `restore` would recreate, without touching the database.
///
/// @param options The CLI options.
/// @param schema The schema the restore targets (see `ResolveRestoreSchema`).
/// @return The process exit code.
int RestoreDryRun(Options const& options, std::string const& schema)
{
    if (!std::filesystem::exists(options.inputFile))
    {
        std::println(std::cerr, "Error: Input file does not exist: {}", options.inputFile.string());
        return EXIT_FAILURE;
    }

    // Read metadata from the backup file
    int err = 0;
    zip_t* zip = zip_open(Lightweight::Zip::ZipPathUtf8(options.inputFile).c_str(), ZIP_RDONLY, &err);
    if (!zip)
    {
        std::println(std::cerr, "Error: Failed to open backup file.");
        return EXIT_FAILURE;
    }

    zip_int64_t const metadataIndex = zip_name_locate(zip, "metadata.json", 0);
    if (metadataIndex < 0)
    {
        std::println(std::cerr, "Error: metadata.json not found in backup.");
        zip_close(zip);
        return EXIT_FAILURE;
    }

    auto const metadataIndexU = static_cast<zip_uint64_t>(metadataIndex);
    zip_stat_t metaStat;
    zip_stat_index(zip, metadataIndexU, 0, &metaStat);

    zip_file_t* file = zip_fopen_index(zip, metadataIndexU, 0);
    if (!file)
    {
        std::println(std::cerr, "Error: Failed to open metadata.json in backup.");
        zip_close(zip);
        return EXIT_FAILURE;
    }
    std::string metadataStr(metaStat.size, '\0');
    zip_fread(file, metadataStr.data(), metaStat.size);
    zip_fclose(file);

    Lightweight::SqlBackup::NullProgressManager nullPm;
    auto tableMap = Lightweight::SqlBackup::ParseSchema(metadataStr, &nullPm);

    // Apply filter
    auto const filter = Lightweight::SqlBackup::TableFilter::Parse(options.filterTables);
    if (!filter.MatchesAll())
        std::erase_if(tableMap, [&](auto const& pair) {
            auto const& [key, info] = pair;
            return !filter.Matches(info.schema.empty() ? schema : info.schema, info.name);
        });

    if (options.schemaOnly)
        std::println(
            "Dry run: Would restore schema only for {} tables from {}", tableMap.size(), options.inputFile.string());
    else
        std::println("Dry run: Would restore {} tables from {}", tableMap.size(), options.inputFile.string());
    std::println("");

    size_t totalRows = 0;
    for (auto const& [tableName, tableInfo]: tableMap)
    {
        totalRows += tableInfo.rowCount;
        std::println("  {} ({} rows, {} columns)", tableName, tableInfo.rowCount, tableInfo.columns.size());
    }
    std::println("");
    std::println("Total: {} tables, {} rows", tableMap.size(), totalRows);

    zip_close(zip);
    return EXIT_SUCCESS;
}

int Restore(Options const& options)
{
    if (options.inputFile.empty())
    {
        std::println(std::cerr, "Error: --input file required for restore.");
        return EXIT_FAILURE;
    }

    auto const schemaResult = ResolveRestoreSchema(options);
    if (!schemaResult)
    {
        std::println(std::cerr, "Error: {}", schemaResult.error());
        return EXIT_FAILURE;
    }
    auto const& schema = *schemaResult;

    if (options.dryRun)
        return RestoreDryRun(options, schema);

    auto pm = CreateProgressManager(options);
    Lightweight::SqlBackup::RetrySettings retrySettings { .maxRetries = options.maxRetries };

    // Parse restore settings from CLI options
    auto restoreSettingsResult = ParseRestoreSettings(options);
    if (!restoreSettingsResult)
    {
        std::println(std::cerr, "Error: {}", restoreSettingsResult.error());
        return EXIT_FAILURE;
    }

    try
    {
        Lightweight::SqlBackup::Restore(options.inputFile,
                                        options.connectionString,
                                        options.jobs,
                                        *pm,
                                        schema,
                                        options.filterTables,
                                        retrySettings,
                                        restoreSettingsResult.value());
    }
    catch (SqlException const& e)
    {
        // SQL-specific exception - format the connection error nicely
        std::println(std::cerr, "Error: {}", FormatConnectionError(e.info().message));
        return EXIT_FAILURE;
    }
    catch (std::runtime_error const& e)
    {
        std::string_view const msg = e.what();
        if (msg.starts_with("Failed to connect"))
            std::println(std::cerr, "Error: {}", FormatConnectionError(msg));
        else
            std::println(std::cerr, "Error: {}", msg);
        return EXIT_FAILURE;
    }

    return pm->ErrorCount() > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

namespace
{

    /// Renders BackupDiff's streamed per-table events to stdout — the console rendering that used to
    /// live inside BackupDiff itself, now a dependency-injected observer so the diff engine stays
    /// output-free and testable.
    class ConsoleBackupDiffObserver final: public Tools::BackupDiffObserver
    {
      public:
        void OnEvent(Tools::BackupDiffEvent const& event) override
        {
            using Kind = Tools::BackupDiffEvent::Kind;
            auto const* const ignoredSuffix = event.ignored ? " (ignored)" : "";
            switch (event.kind)
            {
                case Kind::OnlyInLeft:
                    std::println("[{}] DIFFERENT: present only in left archive{}", event.table, ignoredSuffix);
                    break;
                case Kind::OnlyInRight:
                    std::println("[{}] DIFFERENT: present only in right archive{}", event.table, ignoredSuffix);
                    break;
                case Kind::Identical:
                    std::println("[{}] identical ({} rows)", event.table, event.leftRowCount);
                    break;
                case Kind::ReadError:
                    std::println("[{}] ERROR: failed to read/decode chunk data (left ok={}, right ok={})",
                                 event.table,
                                 event.leftReadOk,
                                 event.rightReadOk);
                    break;
                case Kind::Differing:
                    std::println(
                        "[{}] DIFFERENT: left={} rows, right={} rows; rows-only-in-left={}, rows-only-in-right={}{}",
                        event.table,
                        event.leftRowCount,
                        event.rightRowCount,
                        event.onlyInLeft,
                        event.onlyInRight,
                        ignoredSuffix);
                    for (auto const& ex: event.leftExamples)
                        std::println("    only-in-left  row digest: {}", ex);
                    for (auto const& ex: event.rightExamples)
                        std::println("    only-in-right row digest: {}", ex);
                    break;
            }
        }
    };

} // namespace

/// Implements the `backup-diff` command: compares the DATA CONTENT of two backup archives
/// order-independently to detect silent data corruption. Pure file comparison — opens no DB
/// connection — so it is dispatched in `main()` before `SetupConnectionString`, alongside
/// `resolve-secret` / `list-profiles`.
int BackupDiffCommand(Options const& options)
{
    if (options.leftFile.empty() || options.rightFile.empty())
    {
        std::println(std::cerr, "Error: backup-diff requires both --left <FILE> and --right <FILE>.");
        return EXIT_FAILURE;
    }

    std::println("Comparing backup archives:");
    std::println("  left  (baseline):  {}", options.leftFile.string());
    std::println("  right (candidate): {}", options.rightFile.string());
    std::println("");

    ConsoleBackupDiffObserver observer;
    auto const result = Tools::BackupDiff(options.leftFile, options.rightFile, options.ignoreTables, &observer);

    if (!result.archivesReadable)
    {
        if (!result.leftReadable)
            std::println(std::cerr, "Error: Failed to open backup archive: {}", options.leftFile.string());
        if (!result.rightReadable)
            std::println(std::cerr, "Error: Failed to open backup archive: {}", options.rightFile.string());
        return EXIT_FAILURE;
    }
    if (!result.ignoreTableError.empty())
    {
        std::println(std::cerr, "Error: {}", result.ignoreTableError);
        return EXIT_FAILURE;
    }
    for (auto const& name: result.unmatchedIgnoreTables)
        std::println(std::cerr, "Warning: --ignore-table {} matches no table of either archive.", name);

    std::println("");
    std::println("{} tables compared, {} identical, {} differing ({} ignored)",
                 result.comparedTables,
                 result.identicalTables,
                 result.differingTables,
                 result.ignoredDifferences);

    return result.differenceFound ? EXIT_FAILURE : EXIT_SUCCESS;
}

/// @brief Reads a SQL query from the command argument or, when no argument was
/// supplied (or `-` was passed), from stdin until EOF. Stripped of trailing
/// whitespace so a stray newline doesn't reach the driver.
[[nodiscard]] std::string ResolveExecQueryText(std::string const& argument)
{
    std::string source;
    if (argument.empty() || argument == "-")
    {
        std::string const stdinContent { std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>() };
        source = stdinContent;
    }
    else
    {
        source = argument;
    }
    while (!source.empty()
           && (source.back() == '\n' || source.back() == '\r' || source.back() == ' ' || source.back() == '\t'))
        source.pop_back();
    return source;
}

/// @brief Executes a single SQL statement against the configured connection and
/// streams the result set (if any) to stdout in a tab-separated layout.
///
/// Multi-statement scripts are supported by issuing the whole text as a single
/// `ExecuteDirect` call — the ODBC driver advances through any subsequent result
/// sets automatically. Empty result sets simply print a row count line.
///
/// Useful as a thin diagnostics helper (e.g. inspecting `INFORMATION_SCHEMA`)
/// from CI / shell scripts. Reads the query from `--argument` or, when no
/// argument is supplied, from stdin.
int ExecQuery(Options const& options)
{
    auto const queryText = ResolveExecQueryText(options.argument);
    if (queryText.empty())
    {
        std::println(std::cerr, "Error: exec requires a query — pass it as an argument or via stdin.");
        return EXIT_FAILURE;
    }

    SqlConnection conn;
    SqlStatement stmt(conn);

    try
    {
        auto cursor = stmt.ExecuteDirect(queryText);
        // Print column headers if the statement produced a result set.
        auto const numColumns = cursor.NumColumnsAffected();
        if (numColumns == 0)
        {
            std::println("(no result set)");
            return EXIT_SUCCESS;
        }

        std::size_t rowCount = 0;
        while (cursor.FetchRow())
        {
            for (size_t i = 1; i <= numColumns; ++i)
            {
                if (i != 1)
                    std::print("\t");
                auto const value = cursor.GetNullableColumn<std::string>(static_cast<SQLUSMALLINT>(i));
                std::print("{}", value.value_or(std::string { "(null)" }));
            }
            std::println("");
            ++rowCount;
        }
        std::println(std::cerr, "({} row{})", rowCount, rowCount == 1 ? "" : "s");
        return EXIT_SUCCESS;
    }
    catch (SqlException const& ex)
    {
        std::println(std::cerr, "SQL error: {}", ex.info().message);
        if (!ex.info().sqlState.empty() && ex.info().sqlState != "     ")
            std::println(std::cerr, "  SQL State: {}, Native error: {}", ex.info().sqlState, ex.info().nativeErrorCode);
        return EXIT_FAILURE;
    }
}

/// Owner of the plugins loaded by `GetMigrationManager`, alive until the program exits. The
/// MigrationManager refers to their migrations and compat policy, so the destructor unloads
/// them only after the manager has let go of both.
struct LoadedPluginSet
{
    /// Constructs the manager first, so it is destroyed after this set.
    LoadedPluginSet()
    {
        static_cast<void>(MigrationManager::GetInstance());
    }

    LoadedPluginSet(LoadedPluginSet const&) = delete;
    LoadedPluginSet(LoadedPluginSet&&) = delete;
    LoadedPluginSet& operator=(LoadedPluginSet const&) = delete;
    LoadedPluginSet& operator=(LoadedPluginSet&&) = delete;

    ~LoadedPluginSet()
    {
        Tools::UnloadPlugins(plugins, MigrationManager::GetInstance());
    }

    /// The loaded plugins, in discovery order.
    std::vector<Tools::LoadedPlugin> plugins;
};

/// Plugins loaded by `GetMigrationManager`, kept loaded for the lifetime of the program.
std::vector<Tools::LoadedPlugin>& LoadedPlugins()
{
    static LoadedPluginSet loaded;
    return loaded.plugins;
}

/// Loads the migration plugins into the central manager once, and exits when their post-init
/// hooks would run against a target dbtool must not write to (see `CheckPostInitHookTarget`).
/// Connects nowhere, so `main` calls it before the password is resolved.
/// @param options Options after `ResolveConnectionTargetOptions`.
void IngestCheckedPlugins(Options const& options)
{
    static bool ingested = false;
    if (ingested)
        return;
    ingested = true;

    TraceBreadcrumb("IngestCheckedPlugins: ingesting plugins");
    try
    {
        LoadedPlugins() = Tools::IngestPlugins(options.pluginsDir,
                                               Tools::DefaultPluginLoader(),
                                               MigrationManager::GetInstance(),
                                               MakeIngestOptions(options.verbose));
    }
    catch (std::exception const& e)
    {
        std::println(std::cerr, "Failed to load plugins: {}", e.what());
        std::exit(EXIT_FAILURE);
    }
    if (!CheckPostInitHookTarget(options, LoadedPlugins()))
        std::exit(EXIT_FAILURE);
}

MigrationManager& GetMigrationManager(Options const& options)
{
    auto& plugins = LoadedPlugins();
    static bool initialized = false;

    auto& manager = MigrationManager::GetInstance();
    if (!initialized)
    {
        IngestCheckedPlugins(options);
        // Apply --schema before opening the connection so the post-connect
        // hook is installed prior to the first connect performed by the
        // plugin post-init hooks below.
        if (!options.schema.empty())
        {
            TraceBreadcrumb("GetMigrationManager: setting default schema");
            try
            {
                manager.SetDefaultSchema(options.schema);
            }
            catch (std::invalid_argument const& ex)
            {
                std::println(std::cerr, "Error: {}", ex.what());
                std::exit(EXIT_FAILURE);
            }
        }

        // Route plugin status banners (e.g. SqlMigrationsPlugin's
        // "Transitioning from LASTRADA_PROPERTIES …" line) to stdout so the
        // historical CLI output is preserved exactly. dbtool-gui installs its
        // own sink that routes through `qInfo`.
        InstallStdoutLogSink(manager);

        // Run optional plugin post-init hooks. Each plugin may export a
        // `LightweightMigrationPluginPostInit(SqlConnection&, MigrationManager&)`
        // symbol (via `LIGHTWEIGHT_MIGRATION_PLUGIN_POSTINIT(...)`) for one-shot
        // bridging work that needs both a live connection and the merged
        // migration manager. Per-plugin failures are reported but do not abort
        // dbtool startup — a misbehaving plugin must not be able to brick
        // unrelated commands like `status` or `backup`.
        //
        // We deliberately do *not* call `CreateMigrationHistory()` here: the
        // history table is materialised lazily by the first write path
        // (`ApplySingleMigration`, `MarkMigrationAsApplied`, `RevertSingleMigration`),
        // so read-only commands like `status` or `list-applied` never touch
        // the database with a CREATE TABLE.
        TraceBreadcrumb("GetMigrationManager: running plugin post-init hooks");
        Tools::RunPluginPostInitHooks(plugins, manager.GetDataMapper().Connection(), manager, [](std::string_view message) {
            std::println(std::cerr, "{}", message);
        });

        TraceBreadcrumb("GetMigrationManager: ready");
        initialized = true;
    }
    return manager;
}

/// Runs the plugins' post-migrate hooks once a migrating command has succeeded for real.
/// Callers invoke it while they still hold the migration lock.
///
/// @param manager  The merged migration manager the command ran against.
/// @param exitCode The exit code of the migrating command.
/// @param dryRun   Whether the command only previewed its SQL.
/// @return @p exitCode, or EXIT_FAILURE when a hook failed.
int RunPostMigrateHooks(MigrationManager& manager, int exitCode, bool dryRun)
{
    if (exitCode != EXIT_SUCCESS || dryRun)
        return exitCode;

    TraceBreadcrumb("RunPostMigrateHooks: running plugin post-migrate hooks");
    auto const succeeded = Tools::RunPluginPostMigrateHooks(
        LoadedPlugins(), manager.GetDataMapper().Connection(), manager, [](std::string_view message) {
            std::println(std::cerr, "{}", message);
        });
    if (succeeded)
        return EXIT_SUCCESS;

    std::println(std::cerr, "Error: a plugin post-migrate hook failed; the migrations themselves were applied.");
    return EXIT_FAILURE;
}

/// Warns that a command other than `backup` and `restore` ignores `--all-schemas`.
void WarnIfAllSchemasIgnored(Options const& options)
{
    if (options.allSchemas && options.command != "backup" && options.command != "restore")
        std::println(std::cerr, "Warning: --all-schemas applies to backup only; {} ignores it.", options.command);
}

/// Dispatches a DB-connected command to its handler. Assumes the caller has
/// already established the ODBC connection via `SetupConnectionString`.
int DispatchDbCommand(Options const& options)
{
    if (options.command == "list-pending")
        return ListPendingMigrations(GetMigrationManager(options));
    if (options.command == "list-applied")
        return ListAppliedMigrations(GetMigrationManager(options));
    if (options.command == "status")
        return Status(GetMigrationManager(options));
    if (options.command == "releases")
        return Releases(GetMigrationManager(options));

    if (options.command == "migrate")
    {
        auto& manager = GetMigrationManager(options);
        OptionalScopedLock const lock(manager, options.noLock || options.dryRun);
        auto progress = CommandProgress { options };
        auto const exitCode = Migrate(manager, progress, ExecutionModeOf(options));
        progress.Finish();
        return RunPostMigrateHooks(manager, exitCode, options.dryRun);
    }
    if (options.command == "apply")
    {
        if (options.dryRun)
        {
            std::println(std::cerr, "Error: apply has no dry run; use `migrate --dry-run` to preview pending migrations.");
            return EXIT_FAILURE;
        }
        auto& manager = GetMigrationManager(options);
        OptionalScopedLock const lock(manager, options.noLock);
        auto progress = CommandProgress { options };
        auto const exitCode = ApplyMigration(manager, progress, options.argument);
        progress.Finish();
        return RunPostMigrateHooks(manager, exitCode, /*dryRun=*/false);
    }
    if (options.command == "rollback")
    {
        auto& manager = GetMigrationManager(options);
        OptionalScopedLock const lock(manager, options.noLock);
        return RollbackMigration(manager, options.argument);
    }
    if (options.command == "rollback-to")
    {
        auto& manager = GetMigrationManager(options);
        OptionalScopedLock const lock(manager, options.noLock);
        return RollbackTo(manager, options.argument);
    }
    if (options.command == "rollback-to-release")
    {
        auto& manager = GetMigrationManager(options);
        OptionalScopedLock const lock(manager, options.noLock);
        return RollbackToRelease(manager, options.argument);
    }
    if (options.command == "migrate-to-release")
    {
        auto& manager = GetMigrationManager(options);
        OptionalScopedLock const lock(manager, options.noLock || options.dryRun);
        auto progress = CommandProgress { options };
        auto const exitCode = MigrateToRelease(manager, progress, options.argument, ExecutionModeOf(options));
        progress.Finish();
        return RunPostMigrateHooks(manager, exitCode, options.dryRun);
    }
    if (options.command == "mark-applied")
    {
        auto& manager = GetMigrationManager(options);
        OptionalScopedLock const lock(manager, options.noLock);
        return MarkApplied(manager, options.argument);
    }
    if (options.command == "rewrite-checksums")
    {
        auto& manager = GetMigrationManager(options);
        OptionalScopedLock const lock(manager, options.noLock || options.dryRun);
        return RewriteChecksums(manager, ExecutionModeOf(options), ConfirmationOf(options));
    }
    if (options.command == "hard-reset")
    {
        auto& manager = GetMigrationManager(options);
        OptionalScopedLock const lock(manager, options.noLock || options.dryRun);
        return HardReset(manager, ExecutionModeOf(options), ConfirmationOf(options));
    }
    if (options.command == "unicode-upgrade-tables")
    {
        auto& manager = GetMigrationManager(options);
        OptionalScopedLock const lock(manager, options.noLock || options.dryRun);
        return UnicodeUpgradeTables(manager, options);
    }
    if (options.command == "backup")
        return Backup(options);
    if (options.command == "restore")
        return Restore(options);
    if (options.command == "exec")
        return ExecQuery(options);

    std::println(std::cerr, "Unknown command: {}", options.command);
    return EXIT_FAILURE;
}

/// Handles the `resolve-secret` command, which short-circuits before any DB
/// connection is established so scripts can use dbtool as a thin secret
/// lookup CLI.
int ResolveSecretCommand(Options const& options)
{
    if (options.argument.empty())
    {
        std::println(std::cerr, "Error: resolve-secret requires a <REF> argument (e.g. env:MY_PWD).");
        return EXIT_FAILURE;
    }
    auto const resolver = Lightweight::Secrets::MakeDefaultResolver();
    auto const result = resolver.Resolve(options.argument, options.profileName);
    if (!result)
    {
        std::println(std::cerr, "Error: {}", result.error().message);
        return EXIT_FAILURE;
    }
    std::println("{}", *result);
    return EXIT_SUCCESS;
}

/// Resolves the profile password and installs the connection string. Against a target dbtool
/// must not write to, it creates no missing SQLite file and leaves a discovered dbtool.yml's
/// plaintext password unencrypted.
/// @param options Options after `ResolveConnectionTargetOptions`; updated in place.
/// @return False, after reporting it, when the command cannot connect.
[[nodiscard]] bool PrepareConnection(Options& options)
{
    TraceBreadcrumb("main: resolving profile password");
    auto const passwordOrigin = ApplyProfilePassword(options);
    if (!passwordOrigin)
    {
        std::println(std::cerr, "Error: {}", passwordOrigin.error());
        return false;
    }

    TraceBreadcrumb("main: setting up connection string");
    auto const writableTarget = Lightweight::Config::AllowsWrites(options.targetSource);
    if (!SetupConnectionString(options.connectionString,
                               writableTarget ? MissingSqliteFile::Create : MissingSqliteFile::Leave))
        return false;

    if (*passwordOrigin == Lightweight::Config::PasswordOrigin::Plaintext && writableTarget)
        EncryptPlaintextPasswordIfConnectable(options);
    return true;
}

#if defined(_WIN32)
/// Enables VT sequence processing and UTF-8 output on the Windows console.
void ConfigureWindowsConsole()
{
    if (const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE); hOut != INVALID_HANDLE_VALUE)
        if (DWORD dwMode = 0; GetConsoleMode(hOut, &dwMode) != 0)
            SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    if (const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE); hOut != INVALID_HANDLE_VALUE)
        if (DWORD dwMode = 0; GetConsoleOutputCP() != CP_UTF8)
            SetConsoleOutputCP(CP_UTF8);
}
#endif

} // namespace

int main(int argc, char** argv)
{
    // A command-line tool must never wait for a click, so CRT asserts, aborts and crashes go to stderr.
    Tools::DisableErrorDialogs();

    // Disable stdout buffering for the entire process. With the default fully-buffered
    // mode (or Windows-_IOLBF, which the MSVC runtime silently treats as fully-buffered),
    // std::println output can be silently lost when a loaded DLL — notably psqlODBC's
    // libpq on Windows — performs atexit work that bypasses the C++ runtime's stream
    // flush. Symptom: `dbtool status` against PostgreSQL emits nothing despite running
    // normally and exiting 0. Switching stdout to unbuffered keeps each println visible
    // immediately. dbtool's output volume is low enough that the perf cost is irrelevant.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // Surface Lightweight's warnings (e.g. legacy-migration truncation notices)
    // to stderr. Without this, every `OnWarning` call is routed to the Null logger
    // and silently dropped.
    SqlLogger::SetLogger(SqlLogger::StandardLogger());

    TraceBreadcrumb("main: entered");

    try
    {
        TraceBreadcrumb("main: parsing arguments");
        auto optionsResult = ParseArguments(argc, argv);
        if (!optionsResult)
        {
            std::println(std::cerr, "{}", optionsResult.error());
            return EXIT_FAILURE;
        }
        Options options = optionsResult.value();

        TraceBreadcrumb("main: resolving the connection target");
        // Only for commands that use a profile, so a broken dbtool.yml found by discovery
        // cannot break `help`.
        if (CommandUsesProfile(options.command))
            ResolveConnectionTargetOptions(options);

#if defined(_WIN32)
        TraceBreadcrumb("main: configuring Windows console");
        ConfigureWindowsConsole();
#endif

        if (options.command.empty())
        {
            PrintUsage();
            return EXIT_FAILURE;
        }
        WarnIfAllSchemasIgnored(options);

        if (options.command == "help")
        {
            PrintUsage();
            return EXIT_SUCCESS;
        }

        if (options.command == "show-examples")
        {
            PrintExamples();
            return EXIT_SUCCESS;
        }

        if (options.command == "resolve-secret")
            return ResolveSecretCommand(options);

        if (options.command == "list-profiles")
            return ListProfiles(options);

        if (options.command == "add-profile")
            return AddProfileCommand(options);

        if (options.command == "backup-diff")
            return BackupDiffCommand(options);

        // Before anything connects: even a probe connection creates a missing SQLite file.
        if (!CheckWriteTarget(options))
            return EXIT_FAILURE;

        // Before a `secretRef: stdin:` password prompt: plugins whose post-init hook would run
        // against a target only a discovered default profile supplies refuse the run.
        if (Tools::CommandLoadsPlugins(options.command)
            && options.targetSource == Lightweight::Config::ConnectionTargetSource::DiscoveredConfig)
            IngestCheckedPlugins(options);

        if (!PrepareConnection(options))
            return EXIT_FAILURE;

        TraceBreadcrumb("main: dispatching command");
        auto const rc = DispatchDbCommand(options);
        TraceBreadcrumb("main: dispatch returned, returning to OS");
        return rc;
    }
    catch (Lightweight::SqlMigration::MigrationException const& e)
    {
        // Multi-line structured report so operators immediately see *which*
        // migration failed and *which* statement the driver rejected. The
        // original driver message is preserved verbatim so we don't lose
        // database-specific diagnostics (e.g. MSSQL's "Cannot insert NULL
        // into column X of table Y").
        auto const* const verb =
            e.GetOperation() == Lightweight::SqlMigration::MigrationException::Operation::Apply ? "apply" : "rollback";
        std::println(std::cerr, "Error: failed to {} migration.", verb);
        std::println(std::cerr, "  Migration:    {} - {}", e.GetMigrationTimestamp().value, e.GetMigrationTitle());
        std::println(std::cerr, "  Step:         {}", e.GetStepIndex());
        if (!e.info().sqlState.empty() && e.info().sqlState != "     ")
            std::println(std::cerr, "  SQL State:    {}, Native error: {}", e.info().sqlState, e.info().nativeErrorCode);
        std::println(std::cerr, "  Driver message:");
        std::println(std::cerr, "    {}", e.GetDriverMessage());
        std::println(std::cerr, "  Failed SQL:");
        std::println(std::cerr, "    {}", e.GetFailedSql());
        return EXIT_FAILURE;
    }
    catch (SqlException const& e)
    {
        // SQL-specific exceptions with detailed error info
        std::println(std::cerr, "Database error: {}", e.info().message);
        if (!e.info().sqlState.empty() && e.info().sqlState != "     ")
            std::println(std::cerr, "  SQL State: {}, Native error: {}", e.info().sqlState, e.info().nativeErrorCode);
        return EXIT_FAILURE;
    }
    catch (std::runtime_error const& e)
    {
        // Runtime errors are typically user-facing errors with meaningful messages
        std::println(std::cerr, "Error: {}", e.what());
        return EXIT_FAILURE;
    }
    catch (std::exception const& e)
    {
        // Other unexpected exceptions - show more technical details for debugging
        std::println(std::cerr, "Unexpected error: {}", e.what());
        return EXIT_FAILURE;
    }
}
