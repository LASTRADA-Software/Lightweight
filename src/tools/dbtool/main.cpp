// SPDX-License-Identifier: Apache-2.0
// Force recompile after header change

#include "Lightweight/SqlConnectInfo.hpp"
#include "PluginLoader.hpp"
#include "StandardProgressManager.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlBackup.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlMigrationLock.hpp>
#include <Lightweight/SqlSchema.hpp>

#include <expected>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>
#include <vector>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <yaml-cpp/yaml.h>

#ifdef _WIN32
    #include <io.h>
    #include <shlobj.h>
    #include <windows.h>
#else
    #include <sys/types.h>

    #include <pwd.h>
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
    std::println("  {}list-pending{}             Lists pending migrations", c.command, c.reset);
    std::println("  {}list-applied{}             Lists applied migrations", c.command, c.reset);
    std::println("  {}apply{} {}<TIMESTAMP>{}        Applies the migration with the given timestamp",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}rollback{} {}<TIMESTAMP>{}     Rolls back the migration with the given timestamp",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}rollback-to{} {}<TIMESTAMP>{}  Rolls back all migrations after the given timestamp",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}status{}                   Shows migration status summary", c.command, c.reset);
    std::println("  {}mark-applied{} {}<TIMESTAMP>{} Marks a migration as applied without executing",
                 c.command, c.reset, c.param, c.reset);
    std::println("  {}backup{} --output FILE     Backs up the database to a file", c.command, c.reset);
    std::println("  {}restore{} --input FILE     Restores the database from a file", c.command, c.reset);
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
    std::println("  {}--progress{} {}<TYPE>{}         Progress output type: unicode (default), ascii, logline",
                 c.option, c.reset, c.param, c.reset);
    std::println("  {}--dry-run{}, {}-n{}             Show what would be done without doing it",
                 c.option, c.reset, c.option, c.reset);
    std::println("  {}--no-lock{}                 Skip migration locking for write operations",
                 c.option, c.reset);
    std::println("  {}--quiet{}                   Suppress progress output", c.option, c.reset);
    std::println("  {}--help{}                    Show this help message", c.option, c.reset);
    std::println("");

    std::println("{}Examples:{}", c.heading, c.reset);

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
    std::filesystem::path pluginsDir;
    SqlConnectionString connectionString;
    std::string configFile;
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
    bool pluginsDirSet = false;
    bool connectionStringSet = false;
    bool dryRun = false; ///< If true, show what would be done without actually doing it
    bool noLock = false; ///< If true, skip migration locking for write operations
};

std::filesystem::path GetDefaultConfigPath()
{
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path)))
    {
        return std::filesystem::path(path) / "dbtool" / "dbtool.yml";
    }
    return "dbtool.yml";
#else
    const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfigHome)
    {
        return std::filesystem::path(xdgConfigHome) / "dbtool" / "dbtool.yml";
    }

    char const* home = std::getenv("HOME");
    if (!home)
    {
        struct passwd* pwd = getpwuid(getuid());
        if (pwd)
            home = pwd->pw_dir;
    }

    if (home)
    {
        return std::filesystem::path(home) / ".config" / "dbtool" / "dbtool.yml";
    }

    return "dbtool.yml";
#endif
}

void LoadConfig(Options& options)
{
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
        configPath = GetDefaultConfigPath();
        if (!std::filesystem::exists(configPath))
        {
            return; // No default config, just return
        }
    }

    try
    {
        YAML::Node config = YAML::LoadFile(configPath.string());

        if (config["PluginsDir"] && !options.pluginsDirSet)
        {
            options.pluginsDir = config["PluginsDir"].as<std::string>();
        }

        if (config["ConnectionString"] && !options.connectionStringSet)
        {
            options.connectionString = SqlConnectionString { config["ConnectionString"].as<std::string>() };
        }

        if (config["Schema"] && options.schema.empty())
        {
            options.schema = config["Schema"].as<std::string>();
        }
    }
    catch (std::exception const& e)
    {
        std::println(std::cerr, "Error loading config file: {}", e.what());
        std::exit(EXIT_FAILURE);
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
                    for (auto const* migration: pluginManager->GetAllMigrations())
                    {
                        centralManager.AddMigration(migration);
                    }
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
int Status(MigrationManager& manager)
{
    auto const status = manager.GetMigrationStatus();
    auto const mismatches = manager.VerifyChecksums();

    std::println("Migration Status:");
    std::println("");
    std::println("  Registered migrations: {}", status.totalRegistered);
    std::println("  Applied migrations:    {}", status.appliedCount);
    std::println("  Pending migrations:    {}", status.pendingCount);

    if (status.unknownAppliedCount > 0)
    {
        std::println("  Unknown applied:       {} (applied but not registered)", status.unknownAppliedCount);
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
        std::println(std::cerr, "Error: Failed to rollback migration {}: {}", result.failedAt->value, result.errorMessage);
        std::println(std::cerr,
                     "Rollback stopped. {} migration(s) were rolled back before the failure.",
                     result.revertedTimestamps.size());
        return EXIT_FAILURE;
    }

    std::println("");
    std::println("Successfully rolled back {} migration(s).", result.revertedTimestamps.size());
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
            _issuesByTable[p.tableName].push_back({ p.message, Tools::IssueType::Warning });
        }
        if (p.state == SqlBackup::Progress::State::Error)
        {
            _issuesByTable[p.tableName].push_back({ p.message, Tools::IssueType::Error });
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

    try
    {
        Lightweight::SqlBackup::Restore(options.inputFile,
                                        options.connectionString,
                                        options.jobs,
                                        *pm,
                                        options.schema,
                                        options.filterTables,
                                        retrySettings);
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

} // namespace

int main(int argc, char** argv)
{
    try
    {
        auto optionsResult = ParseArguments(argc, argv);
        if (!optionsResult)
        {
            std::println(std::cerr, "{}", optionsResult.error());
            return EXIT_FAILURE;
        }
        Options options = optionsResult.value();
        LoadConfig(options); // Load config (and fill missing values if not set by CLI)

#if defined(_WIN32)
        // Enable VT support
        if (const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE); hOut != INVALID_HANDLE_VALUE)
            if (DWORD dwMode = 0; GetConsoleMode(hOut, &dwMode) != 0)
                SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

        // Enable UTF-8
        if (const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE); hOut != INVALID_HANDLE_VALUE)
            if (DWORD dwMode = 0; GetConsoleOutputCP() != CP_UTF8)
                SetConsoleOutputCP(CP_UTF8);
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

        if (!SetupConnectionString(options.connectionString))
            return EXIT_FAILURE;

        // Read-only commands (no lock needed)
        if (options.command == "list-pending")
            return ListPendingMigrations(GetMigrationManager(options));
        else if (options.command == "list-applied")
            return ListAppliedMigrations(GetMigrationManager(options));
        else if (options.command == "status")
            return Status(GetMigrationManager(options));

        // Write commands (lock recommended)
        else if (options.command == "migrate")
        {
            auto& manager = GetMigrationManager(options);
            OptionalMigrationLock lock(manager, options.noLock || options.dryRun);
            return Migrate(manager, options.dryRun);
        }
        else if (options.command == "apply")
        {
            auto& manager = GetMigrationManager(options);
            OptionalMigrationLock lock(manager, options.noLock);
            return ApplyMigration(manager, options.argument);
        }
        else if (options.command == "rollback")
        {
            auto& manager = GetMigrationManager(options);
            OptionalMigrationLock lock(manager, options.noLock);
            return RollbackMigration(manager, options.argument);
        }
        else if (options.command == "rollback-to")
        {
            auto& manager = GetMigrationManager(options);
            OptionalMigrationLock lock(manager, options.noLock);
            return RollbackTo(manager, options.argument);
        }
        else if (options.command == "mark-applied")
        {
            auto& manager = GetMigrationManager(options);
            OptionalMigrationLock lock(manager, options.noLock);
            return MarkApplied(manager, options.argument);
        }
        else if (options.command == "backup")
            return Backup(options);
        else if (options.command == "restore")
            return Restore(options);

        std::println(std::cerr, "Unknown command: {}", options.command);
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
