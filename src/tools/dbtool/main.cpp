// SPDX-License-Identifier: Apache-2.0
// Force recompile after header change

#include "DiffRenderer.hpp"
#include "Lightweight/SqlConnectInfo.hpp"
#include "PluginLoader.hpp"
#include "StandardProgressManager.hpp"

#include <Lightweight/Config/ProfileStore.hpp>
#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/Secrets/SecretResolver.hpp>
#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlDataDiff.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlLogger.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlMigrationLock.hpp>
#include <Lightweight/SqlSchema.hpp>
#include <Lightweight/SqlSchemaDiff.hpp>

#include <cstdio>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#ifdef _WIN32
    #include <io.h>
    #include <windows.h>
#else
    #include <unistd.h>
#endif

using namespace Lightweight;
using namespace Lightweight::SqlMigration;
using namespace std::string_view_literals;

namespace
{

bool IsStdoutTerminal()
{
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

struct HelpColors
{
    std::string_view reset;
    std::string_view bold;
    std::string_view dim;
    std::string_view heading; // Section headings like "Commands:", "Options:"
    std::string_view command; // Command names
    std::string_view option;  // Option flags like --output
    std::string_view param;   // Parameter placeholders like <FILE>
    std::string_view example; // Example comments
    std::string_view code;    // Example code/commands

    static HelpColors Colored()
    {
        return {
            .reset = "\033[0m",
            .bold = "\033[1m",
            .dim = "\033[2m",
            .heading = "\033[1;36m", // Bold cyan
            .command = "\033[1;33m", // Bold yellow
            .option = "\033[1;32m",  // Bold green
            .param = "\033[33m",     // Yellow
            .example = "\033[2;37m", // Dim white (for comments)
            .code = "\033[37m",      // White
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
    std::println("  {}diff{} {}<A>{} {}<B>{}            Compares two databases (schema + data) and prints a colored",
                 c.command, c.reset, c.param, c.reset, c.param, c.reset);
    std::println("                            report. Each <SOURCE> is either a profile name or a raw");
    std::println("                            ODBC connection string starting with DRIVER=...");
    std::println("                            Read-only on both sides. Exit code: 0 = equivalent, 1 = differs.");
    std::println("  {}backup{} --output FILE     Backs up the database to a file", c.command, c.reset);
    std::println("  {}restore{} --input FILE     Restores the database from a file", c.command, c.reset);
    std::println("  {}resolve-secret{} {}<REF>{}   Prints a resolved secret to stdout (env:, file:, stdin:)",
                 c.command, c.reset, c.param, c.reset);
    std::println("");

    // Descriptions start at column 29 (longest option is 27 chars + 2 space minimum gap)
    std::println("{}Options:{}", c.heading, c.reset);
    std::println("  {}--plugins-dir{} {}<DIR>{}       Directory to scan for migration plugins",
                 c.option, c.reset, c.param, c.reset);
    std::println("                            (default: current directory)");
    std::println("  {}--connection-string{} {}<STR>{} Connection string",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--schema{} {}<NAME>{}           Database schema to use",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--config{} {}<FILE>{}           Path to configuration file",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--profile{} {}<NAME>{}          Named profile from the config file (default: the file's defaultProfile)",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--output{} {}<FILE>{}           Output file for backup",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--input{} {}<FILE>{}            Input file for restore",
                 c.option, c.reset, c.param, c.reset);
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
    std::println("  {}--dry-run{}, {}-n{}             Show what would be done without doing it",
                 c.option, c.reset, c.option, c.reset);
    std::println("  {}--no-lock{}                 Skip migration locking for write operations",
                 c.option, c.reset);
    std::println("  {}--schema-only{}             Backup/restore schema only, skip data; for `diff` skips the data diff",
                 c.option, c.reset);
    std::println("  {}--no-color{}                Disable ANSI colors in `diff` output (auto-disabled when not at a tty)",
                 c.option, c.reset);
    std::println("  {}--max-rows{} {}<N>{}            Cap rows scanned per table for `diff --data` (default: unlimited)",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--quiet{}                   Suppress progress output", c.option, c.reset);
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

    std::println("  {}# Backup specific tables by name:{}", c.example, c.reset);
    std::println("  {}dbtool backup --output backup.zip --filter-tables=Users,Products{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Backup tables matching wildcard patterns:{}", c.example, c.reset);
    std::println("  {}dbtool backup --output backup.zip --filter-tables=\"*_log,audit*\"{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Backup with schema-qualified table names:{}", c.example, c.reset);
    std::println("  {}dbtool backup --output backup.zip --filter-tables=dbo.Users,sales.*{}", c.code, c.reset);
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

    std::println("  {}# Diff two databases (schema + data) using profile names:{}", c.example, c.reset);
    std::println("  {}dbtool diff prod staging{}", c.code, c.reset);
    std::println("");

    std::println("  {}# Diff using raw connection strings (schema only, no color):{}", c.example, c.reset);
    std::println("  {}{}{}",
                 c.code,
                 R"(dbtool diff "DRIVER=SQLite3;Database=a.db" "DRIVER=SQLite3;Database=b.db" --schema-only --no-color)",
                 c.reset);
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
    std::string secondArgument; ///< Second positional argument (currently used by `diff` for source-B).
    std::filesystem::path pluginsDir;
    SqlConnectionString connectionString;
    std::string configFile;
    std::string profileName; ///< Named profile selected via --profile (empty = ProfileStore default)
    std::filesystem::path outputFile;
    std::filesystem::path inputFile;
    ProgressType progressType = ProgressType::Unicode;
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
    bool dryRun = false;     ///< If true, show what would be done without actually doing it
    bool noLock = false;     ///< If true, skip migration locking for write operations
    bool schemaOnly = false; ///< If true, backup/restore schema only (no data)
    bool yes = false;        ///< If true, confirm destructive actions (e.g. rewrite-checksums)

    /// @brief `--up-to <X>` for migration commands. Empty = no bound.
    std::string upTo;

    /// @brief `--no-color` for `diff` (and other text-rendering commands). When true,
    /// ANSI colors are suppressed.
    bool noColor = false;

    /// @brief `--max-rows <N>` for `diff --data` mode. 0 means unlimited.
    std::size_t diffMaxRows = 0;
};

/// Flattens a parsed profile into an ODBC connection string, mirroring the legacy
/// behaviour of `ApplyProfileToOptions`: prefer an explicit connection string, fall
/// back to a DSN-flavoured profile (which is converted via `SqlConnectionDataSource`).
///
/// Returns an empty connection string when the profile carries neither — callers should
/// fail with a meaningful error in that case.
[[nodiscard]] SqlConnectionString ProfileToConnectionString(Lightweight::Config::Profile const& profile)
{
    if (!profile.connectionString.empty())
        return SqlConnectionString { profile.connectionString };
    if (!profile.dsn.empty())
    {
        SqlConnectionDataSource ds {
            .datasource = profile.dsn,
            .username = profile.uid,
            .password = {},
        };
        return ds.ToConnectionString();
    }
    return SqlConnectionString {};
}

/// Loads a profile from a `ProfileStore` and fills `options` fields that were not
/// already set on the CLI. Supports both the legacy single-profile YAML shape
/// (top-level `PluginsDir` / `ConnectionString` / `Schema`) and the multi-profile
/// shape (`profiles: {name: {...}}`) via `ProfileStore::LoadOrDefault`.
///
/// When `--config` is given but points to a missing file we treat that as a hard
/// error, matching the old loader's behaviour. A missing *default* config is not
/// an error — callers may rely entirely on CLI flags / environment variables.
void ApplyProfileToOptions(Options& options)
{
    namespace Cfg = Lightweight::Config;

    std::filesystem::path configPath;
    if (!options.configFile.empty())
    {
        configPath = options.configFile;
        if (!std::filesystem::exists(configPath))
        {
            std::println(std::cerr, "Error: Config file not found: {}", configPath.string());
            std::exit(EXIT_FAILURE);
        }
    }
    else
    {
        configPath = Cfg::ProfileStore::DefaultPath();
        if (!std::filesystem::exists(configPath))
            return; // No config at all — pure CLI / env mode.
    }

    auto storeResult = Cfg::ProfileStore::LoadOrDefault(configPath);
    if (!storeResult)
    {
        std::println(std::cerr, "Error loading config file: {}", storeResult.error());
        std::exit(EXIT_FAILURE);
    }
    auto const& store = *storeResult;

    Cfg::Profile const* profile = nullptr;
    if (!options.profileName.empty())
    {
        profile = store.Find(options.profileName);
        if (!profile)
        {
            std::println(std::cerr, "Error: profile '{}' not found in {}.", options.profileName, configPath.string());
            std::exit(EXIT_FAILURE);
        }
    }
    else
    {
        profile = store.Default(); // legacy files synthesise a "default" profile
    }

    if (!profile)
        return; // empty store, nothing to apply

    if (!profile->pluginsDir.empty() && !options.pluginsDirSet)
        options.pluginsDir = profile->pluginsDir;

    if (!profile->schema.empty() && options.schema.empty())
        options.schema = profile->schema;

    if (!options.connectionStringSet)
    {
        // Profiles keyed by DSN are flattened into an ODBC connection string so the
        // rest of dbtool (which speaks `SqlConnectionString` everywhere) sees a
        // uniform shape. Secret resolution lands in a follow-up; for now the profile's
        // `secretRef` is ignored and the driver is expected to prompt or fall back to
        // integrated auth.
        auto const cs = ProfileToConnectionString(*profile);
        if (!cs.value.empty())
            options.connectionString = cs;
    }
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
    options.pluginsDir = std::filesystem::current_path();

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
            options.pluginsDir = argv[++i];
            options.pluginsDirSet = true;
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
        else if (arg == "--yes" || arg == "-y")
        {
            options.yes = true;
        }
        else if (arg == "--up-to")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --up-to requires an argument" };
            options.upTo = argv[++i];
        }
        else if (arg == "--no-color")
        {
            options.noColor = true;
        }
        else if (arg == "--max-rows")
        {
            if (i + 1 >= argc)
                return std::unexpected { "Error: --max-rows requires an argument" };
            options.diffMaxRows = static_cast<std::size_t>(std::stoull(argv[++i]));
        }
        else if (options.command.empty())
        {
            options.command = arg;
        }
        else if (options.argument.empty())
        {
            options.argument = arg;
        }
        else if (options.secondArgument.empty())
        {
            options.secondArgument = arg;
        }
        else
        {
            return std::unexpected { std::format("Unknown argument: {}", arg) };
        }
    }
    return options;
}

std::vector<Tools::PluginLoader> LoadPlugins(std::filesystem::path const& dir)
{
    std::vector<Tools::PluginLoader> plugins;
    std::vector<void const*> handles;

    if (!std::filesystem::exists(dir))
    {
        std::println(std::cerr, "Warning: Plugins directory '{}' does not exist.", dir.string());
        return plugins;
    }

    for (auto const& entry: std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        auto const ext = entry.path().extension();
        if (!(ext == ".so" || ext == ".dll" || ext == ".dylib"))
            continue;

        try
        {
            std::println("Loading plugin: {}", entry.path().string());
            Tools::PluginLoader loader { entry.path() };
            void const* const handle = loader.GetNativeHandle();

            // Check if handle already exists
            if (std::ranges::contains(handles, handle))
            {
                std::println(std::cerr, "Failed to load plugin {}: duplicate handle", entry.path().string());
                std::exit(EXIT_FAILURE);
            }

            handles.push_back(handle);
            plugins.push_back(std::move(loader));
        }
        catch (std::exception const& e)
        {
            std::println(std::cerr, "Failed to load plugin {}: {}", entry.path().string(), e.what());
            std::exit(EXIT_FAILURE);
        }
    }
    return plugins;
}

void CollectMigrations(std::vector<Tools::PluginLoader> const& plugins, MigrationManager& centralManager)
{
    for (auto const& plugin: plugins)
    {
        try
        {
            auto const acquireParams = plugin.GetFunction<MigrationManager*()>("AcquireMigrationManager");
            if (acquireParams)
            {
                MigrationManager* pluginManager = acquireParams();
                if (pluginManager && pluginManager != &centralManager)
                {
                    // Register releases before migrations: the failure mode most likely to throw
                    // here is a cross-plugin duplicate release, and we'd rather skip the whole
                    // plugin on conflict than end up with migrations imported but releases missing.
                    for (auto const& release: pluginManager->GetAllReleases())
                    {
                        centralManager.RegisterRelease(release.version, release.highestTimestamp);
                    }
                    for (auto const* migration: pluginManager->GetAllMigrations())
                    {
                        centralManager.AddMigration(migration);
                    }
                    // Propagate any compat policy the plugin installed on its own singleton
                    // manager. Composition lets multiple plugins contribute policies in the
                    // same dbtool process; see `MigrationManager::ComposeCompatPolicy`.
                    if (auto const& policy = pluginManager->GetCompatPolicy())
                        centralManager.ComposeCompatPolicy(policy);
                }
            }
        }
        catch (std::exception const& e)
        {
            std::println(std::cerr, "Error retrieving migrations from plugin: {}", e.what());
        }
    }
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

bool SetupConnectionString(SqlConnectionString const& connectionString)
{
    if (!connectionString.value.empty())
        SqlConnection::SetDefaultConnectionString(connectionString);
    else if (auto const* env = std::getenv("SQL_CONNECTION_STRING"))
        SqlConnection::SetDefaultConnectionString(SqlConnectionString { env });
    else if (auto const* env = std::getenv("ODBC_CONNECTION_STRING"))
        SqlConnection::SetDefaultConnectionString(SqlConnectionString { env });
    else
    {
        std::println(std::cerr,
                     "Error: No connection string provided. Use --connection-string, set SQL_CONNECTION_STRING "
                     "environment variable, or configure it in ~/.config/dbtool/dbtool.yml.\n");
        return false;
    }
    auto const& defaultString = SqlConnection::DefaultConnectionString();
    if (!EnsureSqliteDatabaseFileExists(defaultString))
        std::println(std::cerr, "Warning: could not ensure SQLite database file exists for {}", defaultString.Sanitized());
    return true;
}

int ListPendingMigrations(MigrationManager& manager)
{
    auto const pending = manager.GetPending();
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

int Migrate(MigrationManager& manager, bool dryRun)
{
    if (dryRun)
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
    size_t count = manager.ApplyPendingMigrations([](MigrationBase const& m, size_t i, size_t n) {
        std::println("[{}/{}] Applying {} {}", i + 1, n, m.GetTimestamp().value, m.GetTitle());
    });
    std::println("Applied {} migrations.", count);
    return EXIT_SUCCESS;
}

int ApplyMigration(MigrationManager& manager, std::string_view argument)
{
    return GetMigration(manager, argument)
        .and_then([&manager](MigrationBase const* migration) {
            manager.ApplySingleMigration(*migration);
            std::println("Applied migration {}.", migration->GetTimestamp().value);
            return std::expected<int, std::string> { EXIT_SUCCESS };
        })
        .transform_error([](std::string const& error) {
            std::println(std::cerr, "Error: {}", error);
            return EXIT_FAILURE;
        })
        .value();
}

int RollbackMigration(MigrationManager& manager, std::string_view argument)
{
    return GetMigration(manager, argument)
        .and_then([&manager](MigrationBase const* migration) {
            manager.RevertSingleMigration(*migration);
            std::println("Rolled back migration {}.", migration->GetTimestamp().value);
            return std::expected<int, std::string> { EXIT_SUCCESS };
        })
        .transform_error([](std::string const& error) {
            std::println(std::cerr, "Error: {}", error);
            return EXIT_FAILURE;
        })
        .value();
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
        std::println("  {:<{}}{} (applied but not registered)",
                     "Unknown applied:",
                     labelWidth,
                     status.unknownAppliedCount);
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
            auto const* const label =
                (latestApplied.applied == latestApplied.total) ? "applied" : "partially applied";
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

/// @brief Generic "render plan → if empty, exit success → print diff → if dry-run,
/// exit success → if not `--yes`, refuse → execute → print summary" loop, factored
/// out so `rewrite-checksums`, `hard-reset`, and `unicode-upgrade-tables` share
/// the same UX without duplicating it.
///
/// The caller supplies the four template hooks via lambdas — `run` is invoked
/// twice (once with `dryRun=true`, once with `dryRun=false`) so the manager-level
/// API doesn't have to memoize results across calls.
template <typename Result>
int RunAdminCommand(std::string_view name,
                     bool dryRun,
                     bool yes,
                     auto&& runFn,
                     auto&& isEmptyFn,
                     auto&& printDiffFn,
                     auto&& printSummaryFn)
{
    auto const preview = runFn(/*dryRun=*/true);

    if (isEmptyFn(preview))
    {
        std::println("No {} changes needed. Nothing to do.", name);
        return EXIT_SUCCESS;
    }

    printDiffFn(preview);

    if (dryRun)
    {
        std::println("Dry run: nothing written. Re-run without --dry-run to apply.");
        return EXIT_SUCCESS;
    }

    if (!yes)
    {
        std::println("Refusing to {} without --yes. Pass --yes to confirm.", name);
        return EXIT_FAILURE;
    }

    auto const written = runFn(/*dryRun=*/false);
    printSummaryFn(written);
    return EXIT_SUCCESS;
}

/// Rewrites stored checksums in `schema_migrations` to match the current code.
///
/// Used after a regen of generated migrations changes byte shape but not logical
/// effect (the Unicode-default flip in `lup2dbtool` is the canonical case). The
/// caller is expected to have verified out-of-band that the regen is logically
/// equivalent — this command is a recovery tool, not a maintenance loop.
///
/// In dry-run mode the diff is printed and nothing is written. Without dry-run the
/// caller must explicitly confirm; otherwise the command exits without writing.
int RewriteChecksums(MigrationManager& manager, bool dryRun, bool yes)
{
    return RunAdminCommand<MigrationManager::RewriteChecksumsResult>(
        "rewrite-checksums",
        dryRun,
        yes,
        [&](bool dr) { return manager.RewriteChecksums(dr); },
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
int HardReset(MigrationManager& manager, bool dryRun, bool yes)
{
    return RunAdminCommand<MigrationManager::HardResetResult>(
        "hard-reset",
        dryRun,
        yes,
        [&](bool dr) { return manager.HardReset(dr); },
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
            std::println("Dropped {} table(s){}.", r.droppedTables.size(),
                         r.schemaMigrationsDropped ? " plus schema_migrations" : "");
        });
}

/// @brief Upgrades legacy VARCHAR/CHAR columns to NVARCHAR/NCHAR.
int UnicodeUpgradeTables(MigrationManager& manager, bool dryRun, bool yes)
{
    return RunAdminCommand<MigrationManager::UnicodeUpgradeResult>(
        "unicode-upgrade-tables",
        dryRun,
        yes,
        [&](bool dr) { return manager.UnicodeUpgradeTables(dr); },
        [](auto const& r) { return r.columns.empty(); },
        [](auto const& r) {
            std::println("Columns to upgrade ({}):", r.columns.size());
            for (auto const& c: r.columns)
                std::println("  {}.{}", c.table.table, c.column);
            if (!r.rebuiltForeignKeys.empty())
            {
                std::println("");
                std::println("Foreign keys to drop and re-add ({}):", r.rebuiltForeignKeys.size());
                for (auto const& fk: r.rebuiltForeignKeys)
                {
                    std::print("  ");
                    for (auto const& c: fk.columns)
                        std::print("{} ", c);
                    std::println("→ {}", fk.referencedTableName);
                }
            }
        },
        [](auto const& r) {
            std::set<std::string> tables;
            for (auto const& c: r.columns)
                tables.insert(c.table.table);
            std::println("Upgraded {} column(s) across {} table(s).", r.columns.size(), tables.size());
        });
}

/// Marks a migration as applied without executing its Up() method.
///
/// @param manager The migration manager instance.
/// @param argument The migration timestamp to mark as applied.
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
int MarkApplied(MigrationManager& manager, std::string_view argument)
{
    return GetMigration(manager, argument)
        .and_then([&manager](MigrationBase const* migration) -> std::expected<int, std::string> {
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
        })
        .transform_error([](std::string const& error) {
            std::println(std::cerr, "Error: {}", error);
            return EXIT_FAILURE;
        })
        .value();
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
    std::println("Successfully rolled back {} migration(s).", result.revertedTimestamps.size());
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
/// @param dryRun When true, print the SQL that would be executed without touching the database.
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
int MigrateToRelease(MigrationManager& manager, std::string_view argument, bool dryRun)
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
    auto const highestApplied = std::ranges::max_element(
        applied, {}, [](MigrationTimestamp ts) { return ts.value; });
    if (highestApplied != applied.end() && highestApplied->value >= release->highestTimestamp.value)
    {
        std::println("Database is already at or past release '{}' (highest applied: {}, release boundary: {}).",
                     argument,
                     highestApplied->value,
                     release->highestTimestamp.value);
        std::println("Use `dbtool rollback-to-release {}` to go back.", argument);
        return EXIT_SUCCESS;
    }

    if (dryRun)
    {
        std::println("-- Dry run: SQL that would be executed up to release '{}' (timestamp {})\n",
                     release->version,
                     release->highestTimestamp.value);
        auto statements = manager.PreviewPendingMigrationsUpTo(
            release->highestTimestamp, [](MigrationBase const& m, size_t i, size_t n) {
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
    size_t const count =
        manager.ApplyPendingMigrationsUpTo(release->highestTimestamp, [](MigrationBase const& m, size_t i, size_t n) {
            std::println("[{}/{}] Applying {} {}", i + 1, n, m.GetTimestamp().value, m.GetTitle());
        });
    std::println("Applied {} migration(s) up to release '{}'.", count, argument);
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
    std::println("Successfully rolled back {} migration(s) to release '{}'.", result.revertedTimestamps.size(), argument);
    return EXIT_SUCCESS;
}

/// RAII wrapper for optional migration locking.
///
/// Acquires a migration lock if locking is enabled, otherwise does nothing.
/// The lock is automatically released when the wrapper goes out of scope.
class OptionalMigrationLock
{
  public:
    OptionalMigrationLock(MigrationManager& manager, bool noLock):
        _lock(noLock ? std::nullopt : std::make_optional<MigrationLock>(manager.GetDataMapper().Connection()))
    {
        if (_lock && !_lock->IsLocked())
        {
            throw std::runtime_error("Failed to acquire migration lock. Another migration may be in progress.");
        }
    }

  private:
    std::optional<MigrationLock> _lock;
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

    double value = 0;
    auto const numPart = sizeStr.substr(0, numEnd);
    auto const [ptr, ec] = std::from_chars(numPart.data(), numPart.data() + numPart.size(), value);
    if (ec != std::errc {} || ptr != numPart.data() + numPart.size())
        return std::unexpected { std::format("Invalid size '{}': invalid number", sizeStr) };

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

int Backup(Options const& options)
{
    if (options.outputFile.empty())
    {
        std::println(std::cerr, "Error: --output file required for backup.");
        return EXIT_FAILURE;
    }

    // Parse and validate backup settings (compression + chunk size)
    auto backupSettingsResult = ParseBackupSettings(options);
    if (!backupSettingsResult)
    {
        std::println(std::cerr, "Error: {}", backupSettingsResult.error());
        return EXIT_FAILURE;
    }
    auto const& backupSettings = backupSettingsResult.value();

    if (options.dryRun)
    {
        // Dry run: show what would be backed up
        SqlConnection conn { std::nullopt };
        if (!conn.Connect(options.connectionString))
        {
            std::println(std::cerr,
                         "Error: {}",
                         FormatConnectionError(std::format("Failed to connect to database: {}", conn.LastError().message)));
            return EXIT_FAILURE;
        }

        auto pm = CreateProgressManager(options);

        // Parse filter early to use as predicate during schema scanning
        auto const filter = Lightweight::SqlBackup::TableFilter::Parse(options.filterTables);

        // Create table filter predicate to skip reading schema for non-matching tables
        SqlSchema::TableFilterPredicate tableFilterPredicate;
        if (!filter.MatchesAll())
        {
            tableFilterPredicate = [&filter, &options](std::string_view /*schemaName*/, std::string_view tableName) {
                return filter.Matches(options.schema, tableName);
            };
        }

        SqlStatement stmt { conn };
        SqlSchema::ReadAllTablesCallback schemaCallback =
            [&pm](std::string_view tableName, size_t const current, size_t const total) {
                pm->Update({ .state = SqlBackup::Progress::State::InProgress,
                             .tableName = "Scanning schema",
                             .currentRows = current,
                             .totalRows = total,
                             .message = std::format("Scanning table {}", tableName) });
            };
        auto tables =
            SqlSchema::ReadAllTables(stmt, conn.DatabaseName(), options.schema, schemaCallback, {}, tableFilterPredicate);

        pm->Update({ .state = SqlBackup::Progress::State::Finished,
                     .tableName = "Scanning schema",
                     .currentRows = tables.size(),
                     .totalRows = tables.size(),
                     .message = "" });

        pm->AllDone();

        std::println("");
        if (options.schemaOnly)
            std::println(
                "Dry run: Would backup schema only for {} tables to {}", tables.size(), options.outputFile.string());
        else
            std::println("Dry run: Would backup {} tables to {}", tables.size(), options.outputFile.string());
        std::println("");

        size_t totalRows = 0;
        for (auto const& table: tables)
        {
            auto rowCount =
                stmt.ExecuteDirectScalar<int64_t>(std::format("SELECT COUNT(*) FROM \"{}\"", table.name)).value_or(0);
            totalRows += static_cast<size_t>(rowCount);
            std::println("  {} ({} rows, {} columns)", table.name, rowCount, table.columns.size());
        }
        std::println("");
        std::println("Total: {} tables, {} rows", tables.size(), totalRows);

        return EXIT_SUCCESS;
    }

    auto pm = CreateProgressManager(options);
    Lightweight::SqlBackup::RetrySettings retrySettings { .maxRetries = options.maxRetries };

    try
    {
        Lightweight::SqlBackup::Backup(options.outputFile,
                                       options.connectionString,
                                       options.jobs,
                                       *pm,
                                       options.schema,
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

int Restore(Options const& options)
{
    if (options.inputFile.empty())
    {
        std::println(std::cerr, "Error: --input file required for restore.");
        return EXIT_FAILURE;
    }

    if (options.dryRun)
    {
        // Dry run: show what would be restored
        if (!std::filesystem::exists(options.inputFile))
        {
            std::println(std::cerr, "Error: Input file does not exist: {}", options.inputFile.string());
            return EXIT_FAILURE;
        }

        // Read metadata from the backup file
        int err = 0;
        zip_t* zip = zip_open(options.inputFile.string().c_str(), ZIP_RDONLY, &err);
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
            std::erase_if(tableMap, [&](auto const& pair) { return !filter.Matches(options.schema, pair.first); });

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
                                        options.schema,
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
    while (!source.empty() && (source.back() == '\n' || source.back() == '\r' || source.back() == ' ' || source.back() == '\t'))
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

/// Builds a single InProgress event for the schema-read or data-diff phases. Centralised
/// here so the field-set (which clang's designated-init warning is picky about) lives in
/// one place.
[[nodiscard]] SqlBackup::Progress MakeProgressEvent(
    std::string tableName,
    std::size_t current,
    std::optional<std::size_t> total = std::nullopt,
    SqlBackup::Progress::State state = SqlBackup::Progress::State::InProgress)
{
    auto p = SqlBackup::Progress {};
    p.state = state;
    p.tableName = std::move(tableName);
    p.currentRows = current;
    p.totalRows = total;
    return p;
}

/// Walks every table on side A whose name is also present on side B and runs
/// `SqlSchema::DiffTableData`, accumulating non-empty results. Live progress events are
/// fed into @p progress so the user sees current row counts and an ETA.
[[nodiscard]] std::vector<SqlSchema::TableDataDiff> DiffSharedTables(SqlConnection& connA,
                                                                     SqlConnection& connB,
                                                                     SqlSchema::TableList const& tablesA,
                                                                     SqlSchema::TableList const& tablesB,
                                                                     std::size_t maxRows,
                                                                     SqlBackup::ProgressManager* progress)
{
    auto tablesByNameB = std::map<std::string, SqlSchema::Table const*> {};
    for (auto const& t: tablesB)
        tablesByNameB.emplace(t.name, &t);

    auto onProgress = [&](SqlSchema::DiffProgressEvent const& ev) {
        if (!progress)
            return;
        progress->Update(MakeProgressEvent(std::format("data: {}", ev.tableName), ev.rowsScannedA + ev.rowsScannedB));
    };

    auto diffs = std::vector<SqlSchema::TableDataDiff> {};
    for (auto const& tA: tablesA)
    {
        auto const itB = tablesByNameB.find(tA.name);
        if (itB == tablesByNameB.end())
            continue; // Schema diff already reports "only in A" — skip.
        auto const& tB = *itB->second;

        // Catch per-table errors so one un-diffable table doesn't abort the whole run.
        // Driver-side coercion failures (e.g. "Numeric value out of range" on wide numeric
        // columns) get reported as a per-table `skipReason` and the diff continues.
        auto diff = SqlSchema::TableDataDiff { .tableName = tA.name };
        try
        {
            diff = SqlSchema::DiffTableData(connA, connB, tA, tB, maxRows, onProgress);
        }
        catch (Lightweight::SqlException const& ex)
        {
            diff.skipReason = std::format("error: {}", ex.info().message);
        }

        if (progress)
            progress->Update(MakeProgressEvent(std::format("data: {}", tA.name),
                                               diff.aRowCount + diff.bRowCount,
                                               std::nullopt,
                                               SqlBackup::Progress::State::Finished));
        if (!diff.rows.empty() || diff.skipReason.has_value())
            diffs.push_back(std::move(diff));
    }
    return diffs;
}

/// Decides whether @p token is a raw ODBC connection string (`DRIVER=...`) or a
/// profile name to look up via `Cfg::ProfileStore`. Returns the resolved connection
/// string, or an error string suitable for printing to stderr.
[[nodiscard]] std::expected<SqlConnectionString, std::string> ResolveDiffSource(std::string_view token,
                                                                                Options const& options)
{
    namespace Cfg = Lightweight::Config;

    // Heuristic: ODBC connection strings start with `DRIVER=` (case-insensitive).
    auto looksLikeOdbcCs = [](std::string_view t) {
        constexpr auto kExpected = std::string_view { "DRIVER=" };
        if (t.size() < kExpected.size())
            return false;
        for (std::size_t i = 0; i < kExpected.size(); ++i)
        {
            auto const lhs = static_cast<char>(std::tolower(static_cast<unsigned char>(t[i])));
            auto const rhs = static_cast<char>(std::tolower(static_cast<unsigned char>(kExpected[i])));
            if (lhs != rhs)
                return false;
        }
        return true;
    };

    if (looksLikeOdbcCs(token))
        return SqlConnectionString { std::string { token } };

    // Treat as profile name. Resolve through the same `ProfileStore` as the rest of dbtool,
    // honoring `--config` if the user passed one.
    auto configPath =
        !options.configFile.empty() ? std::filesystem::path { options.configFile } : Cfg::ProfileStore::DefaultPath();
    if (!std::filesystem::exists(configPath))
        return std::unexpected { std::format(
            "Error: profile '{}' requested but no config file at {}", token, configPath.string()) };

    auto storeResult = Cfg::ProfileStore::LoadOrDefault(configPath);
    if (!storeResult)
        return std::unexpected { std::format("Error loading config file: {}", storeResult.error()) };

    auto const* profile = storeResult->Find(std::string { token });
    if (!profile)
        return std::unexpected { std::format("Error: profile '{}' not found in {}", token, configPath.string()) };

    auto cs = ProfileToConnectionString(*profile);
    if (cs.value.empty())
        return std::unexpected { std::format("Error: profile '{}' has no connectionString or dsn", token) };
    return cs;
}

/// Implements `dbtool diff <SOURCE-A> <SOURCE-B>`. Read-only; opens two independent
/// connections (no global default), runs the schema diff and (unless --schema-only)
/// the data diff, prints a colored report to stdout, and returns a non-zero exit code
/// when any differences were found.
int DiffDatabases(Options const& options)
{
    if (options.argument.empty() || options.secondArgument.empty())
    {
        std::println(std::cerr, "Error: diff requires two source arguments (profile name or DRIVER=... connection string).");
        std::println(std::cerr, "Usage: dbtool diff <SOURCE-A> <SOURCE-B> [--schema-only] [--no-color] [--max-rows N]");
        return EXIT_FAILURE;
    }

    auto const csA = ResolveDiffSource(options.argument, options);
    if (!csA)
    {
        std::println(std::cerr, "{}", csA.error());
        return EXIT_FAILURE;
    }
    auto const csB = ResolveDiffSource(options.secondArgument, options);
    if (!csB)
    {
        std::println(std::cerr, "{}", csB.error());
        return EXIT_FAILURE;
    }

    try
    {
        auto connA = SqlConnection { *csA };
        auto connB = SqlConnection { *csB };
        if (!connA.IsAlive())
        {
            std::println(std::cerr, "Error: failed to connect to source A.");
            return EXIT_FAILURE;
        }
        if (!connB.IsAlive())
        {
            std::println(std::cerr, "Error: failed to connect to source B.");
            return EXIT_FAILURE;
        }

        auto progress = CreateProgressManager(options);

        // Schema phase — feed the per-table callbacks of `ReadAllTables` into the
        // progress manager so the user sees "n/total" while introspection runs.
        auto stmtA = SqlStatement { connA };
        auto stmtB = SqlStatement { connB };

        auto schemaProgressFor = [&](std::string const& side) {
            return [&, side](std::string_view tableName, std::size_t current, std::size_t total) {
                if (progress)
                    progress->Update(MakeProgressEvent(std::format("{}: {}", side, tableName), current, total));
            };
        };

        auto const tablesA =
            SqlSchema::ReadAllTables(stmtA, connA.DatabaseName(), options.schema, schemaProgressFor("schema A"));
        auto const tablesB =
            SqlSchema::ReadAllTables(stmtB, connB.DatabaseName(), options.schema, schemaProgressFor("schema B"));

        auto const schemaDiff = SqlSchema::DiffSchemas(tablesA, tablesB);

        auto dataDiffs = options.schemaOnly
                             ? std::vector<SqlSchema::TableDataDiff> {}
                             : DiffSharedTables(connA, connB, tablesA, tablesB, options.diffMaxRows, progress.get());

        if (progress)
            progress->AllDone();

        auto const useColor = !options.noColor && (isatty(fileno(stdout)) != 0);
        auto const renderOpts = Lightweight::Tools::DiffRenderOptions { .useColor = useColor };
        Lightweight::Tools::RenderDiff(std::cout, schemaDiff, dataDiffs, renderOpts);

        auto const anyDataDiff =
            std::ranges::any_of(dataDiffs, [](SqlSchema::TableDataDiff const& d) { return !d.rows.empty(); });
        return (schemaDiff.Empty() && !anyDataDiff) ? EXIT_SUCCESS : 1;
    }
    catch (SqlException const& ex)
    {
        std::println(std::cerr, "SQL error: {}", ex.info().message);
        return EXIT_FAILURE;
    }
}

MigrationManager& GetMigrationManager(Options const& options)
{
    // Keep plugins loaded for the lifetime of the program.
    // The MigrationManager holds references to migration code from these plugins,
    // so they must not be unloaded (via dlclose) while migrations may still be accessed.
    static std::vector<Tools::PluginLoader> plugins;
    static bool initialized = false;

    auto& manager = MigrationManager::GetInstance();
    if (!initialized)
    {
        plugins = LoadPlugins(options.pluginsDir);
        CollectMigrations(plugins, manager);
        manager.CreateMigrationHistory();
        initialized = true;
    }
    return manager;
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
        OptionalMigrationLock const lock(manager, options.noLock || options.dryRun);
        return Migrate(manager, options.dryRun);
    }
    if (options.command == "apply")
    {
        auto& manager = GetMigrationManager(options);
        OptionalMigrationLock const lock(manager, options.noLock);
        return ApplyMigration(manager, options.argument);
    }
    if (options.command == "rollback")
    {
        auto& manager = GetMigrationManager(options);
        OptionalMigrationLock const lock(manager, options.noLock);
        return RollbackMigration(manager, options.argument);
    }
    if (options.command == "rollback-to")
    {
        auto& manager = GetMigrationManager(options);
        OptionalMigrationLock const lock(manager, options.noLock);
        return RollbackTo(manager, options.argument);
    }
    if (options.command == "rollback-to-release")
    {
        auto& manager = GetMigrationManager(options);
        OptionalMigrationLock const lock(manager, options.noLock);
        return RollbackToRelease(manager, options.argument);
    }
    if (options.command == "migrate-to-release")
    {
        auto& manager = GetMigrationManager(options);
        OptionalMigrationLock const lock(manager, options.noLock || options.dryRun);
        return MigrateToRelease(manager, options.argument, options.dryRun);
    }
    if (options.command == "mark-applied")
    {
        auto& manager = GetMigrationManager(options);
        OptionalMigrationLock const lock(manager, options.noLock);
        return MarkApplied(manager, options.argument);
    }
    if (options.command == "rewrite-checksums")
    {
        auto& manager = GetMigrationManager(options);
        OptionalMigrationLock const lock(manager, options.noLock || options.dryRun);
        return RewriteChecksums(manager, options.dryRun, options.yes);
    }
    if (options.command == "hard-reset")
    {
        auto& manager = GetMigrationManager(options);
        OptionalMigrationLock const lock(manager, options.noLock || options.dryRun);
        return HardReset(manager, options.dryRun, options.yes);
    }
    if (options.command == "unicode-upgrade-tables")
    {
        auto& manager = GetMigrationManager(options);
        OptionalMigrationLock const lock(manager, options.noLock || options.dryRun);
        return UnicodeUpgradeTables(manager, options.dryRun, options.yes);
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

    try
    {
        auto optionsResult = ParseArguments(argc, argv);
        if (!optionsResult)
        {
            std::println(std::cerr, "{}", optionsResult.error());
            return EXIT_FAILURE;
        }
        Options options = optionsResult.value();
        ApplyProfileToOptions(options); // Resolve a named (or default) profile and fill unset fields.

#if defined(_WIN32)
        ConfigureWindowsConsole();
#endif

        if (options.command.empty())
        {
            PrintUsage();
            return EXIT_FAILURE;
        }

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

        // `diff` opens its own two connections from the positional args and does not
        // use the global default connection string. Skip the SetupConnectionString
        // gate, which would otherwise reject invocations that pass no `--connection-string`.
        if (options.command == "diff")
            return DiffDatabases(options);

        if (!SetupConnectionString(options.connectionString))
            return EXIT_FAILURE;

        return DispatchDbCommand(options);
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
