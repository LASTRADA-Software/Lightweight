#include "PluginLoader.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#ifdef _WIN32
    #include <shlobj.h>
    #include <windows.h>
#else
    #include <sys/types.h>

    #include <pwd.h>
    #include <unistd.h>
#endif

using namespace Lightweight;
using namespace Lightweight::SqlMigration;

namespace
{

void PrintUsage()
{
    std::println("Usage: dbtool <command> [options]\n"
                 "\n"
                 "Commands:\n"
                 "  migrate                Applies pending migrations\n"
                 "  list-pending           Lists pending migrations\n"
                 "  list-applied           Lists applied migrations\n"
                 "  apply <TIMESTAMP>      Applies the migration with the given timestamp\n"
                 "  rollback <TIMESTAMP>   Rolls back the migration with the given timestamp\n"
                 "\n"
                 "Options:\n"
                 "  --plugins-dir <DIR>    Directory to scan for migration plugins (default: current directory)\n"
                 "  --connection-string <STR> Connection string\n"
                 "  --config <FILE>        Path to configuration file\n"
                 "  --help                 Show this help message\n"
                 "\n"
                 "Examples:\n"
                 "  # Apply pending migrations using an SQLite database file via ODBC:\n"
                 "  dbtool migrate --connection-string \"DRIVER=SQLite3;Database=test.db\"\n"
                 "\n"
                 "  # List pending migrations for a specific plugins directory:\n"
                 "  dbtool list-pending --plugins-dir ./plugins\n"
                 "\n"
                 "  # Rollback a specific migration:\n"
                 "  dbtool rollback 20230101000000 --connection-string \"DRIVER=SQLite3;Database=test.db\"\n");
}

struct Options
{
    std::string command;
    std::string argument;
    std::filesystem::path pluginsDir;
    std::string connectionString;
    std::string configFile;
    bool pluginsDirSet = false;
    bool connectionStringSet = false;
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
            options.connectionString = config["ConnectionString"].as<std::string>();
        }
    }
    catch (std::exception const& e)
    {
        std::println(std::cerr, "Error loading config file: {}", e.what());
        std::exit(EXIT_FAILURE);
    }
}

Options ParseArguments(int argc, char** argv)
{
    Options options;
    // First pass to get config file
    for (int i = 1; i < argc; ++i)
    {
        std::string const arg = argv[i];
        if (arg == "--config" && i + 1 < argc)
        {
            options.configFile = argv[++i];
        }
    }

    // Initialize defaults before loading config
    options.pluginsDir = std::filesystem::current_path();

    // Second pass to get actual arguments and override config
    for (int i = 1; i < argc; ++i)
    {
        std::string const arg = argv[i];
        if (arg == "--help")
        {
            PrintUsage();
            std::exit(0);
        }
        else if (arg == "--plugins-dir")
        {
            if (i + 1 < argc)
            {
                options.pluginsDir = argv[++i];
                options.pluginsDirSet = true;
            }
            else
            {
                std::println(std::cerr, "Error: --plugins-dir requires an argument");
                std::exit(1);
            }
        }
        else if (arg == "--connection-string")
        {
            if (i + 1 < argc)
            {
                options.connectionString = argv[++i];
                options.connectionStringSet = true;
            }
            else
            {
                std::println(std::cerr, "Error: --connection-string requires an argument");
                std::exit(1);
            }
        }
        else if (arg == "--config")
        {
            i++; // Skip argument
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
            std::println(std::cerr, "Unknown argument: {}", arg);
            std::exit(1);
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

bool SetupConnectionString(std::string& connectionString)
{
    if (!connectionString.empty())
        SqlConnection::SetDefaultConnectionString(SqlConnectionString { connectionString });
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

int Migrate(MigrationManager& manager)
{
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

} // namespace

int main(int argc, char** argv)
{
    try
    {
        Options options = ParseArguments(argc, argv);
        LoadConfig(options); // Load config (and fill missing values if not set by CLI)

        if (options.command.empty())
        {
            PrintUsage();
            return EXIT_FAILURE;
        }

        if (!SetupConnectionString(options.connectionString))
            return EXIT_FAILURE;

        // Ensure migration history table exists
        MigrationManager::GetInstance().CreateMigrationHistory();

        std::vector<Tools::PluginLoader> plugins = LoadPlugins(options.pluginsDir);
        CollectMigrations(plugins, MigrationManager::GetInstance());

        MigrationManager& manager = MigrationManager::GetInstance();

        if (options.command == "list-pending")
            return ListPendingMigrations(manager);
        else if (options.command == "list-applied")
            return ListAppliedMigrations(manager);
        else if (options.command == "migrate")
            return Migrate(manager);
        else if (options.command == "apply")
            return ApplyMigration(manager, options.argument);
        else if (options.command == "rollback")
            return RollbackMigration(manager, options.argument);

        std::println(std::cerr, "Unknown command: {}", options.command);
        return EXIT_FAILURE;
    }
    catch (std::exception const& e)
    {
        std::println(std::cerr, "Fatal Error: {}", e.what());
        return EXIT_FAILURE;
    }
}
