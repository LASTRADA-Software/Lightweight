// SPDX-License-Identifier: Apache-2.0
//
// Example executable that loads the chinook_migrations plugin at runtime
// and prints the list of migrations and releases.
//
// Usage:
//   chinook_migrate [--db PATH] [--plugin PATH] [--apply]
//
// Defaults:
//   --db       chinook.db (SQLite file in current directory)
//   --plugin   <exe-dir>/libchinook_migrations.{so,dll,dylib}

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

#include "../../../tools/dbtool/PluginLoader.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace Lightweight;
using SqlMigration::MigrationManager;
using SqlMigration::MigrationTimestamp;

namespace fs = std::filesystem;

namespace
{

constexpr std::string_view SqliteDriver =
#if defined(_WIN32) || defined(_WIN64)
    "SQLite3 ODBC Driver";
#else
    "SQLite3";
#endif

constexpr std::string_view DefaultPluginName =
#if defined(_WIN32) || defined(_WIN64)
    "chinook_migrations.dll";
#elif defined(__APPLE__)
    "libchinook_migrations.dylib";
#else
    "libchinook_migrations.so";
#endif

struct Options
{
    fs::path database = "chinook.db";
    fs::path plugin;
    bool apply = false;
};

Options ParseArgs(std::span<char*> argv, fs::path const& exeDir)
{
    Options opts;
    for (size_t i = 1; i < argv.size(); ++i)
    {
        std::string_view arg = argv[i];
        auto requireValue = [&]() -> std::string_view {
            if (i + 1 >= argv.size())
                throw std::runtime_error(std::format("Missing value for {}", arg));
            return argv[++i];
        };
        if (arg == "--db" || arg == "--database")
        {
            opts.database = requireValue();
        }
        else if (arg == "--plugin")
        {
            opts.plugin = requireValue();
        }
        else if (arg == "--apply")
        {
            opts.apply = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::println("Usage: chinook_migrate [--db PATH] [--plugin PATH] [--apply]");
            std::exit(0);
        }
        else
        {
            throw std::runtime_error(std::format("Unknown argument: {}", arg));
        }
    }
    if (opts.plugin.empty())
        opts.plugin = exeDir / DefaultPluginName;
    return opts;
}

bool IsApplied(std::vector<MigrationTimestamp> const& applied, MigrationTimestamp ts)
{
    return std::ranges::any_of(applied, [&](auto a) { return a.value == ts.value; });
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        auto const exeDir = fs::weakly_canonical(fs::path(argv[0])).parent_path();
        auto const opts = ParseArgs({ argv, static_cast<size_t>(argc) }, exeDir);

        std::println("Database : {}", fs::absolute(opts.database).string());
        std::println("Plugin   : {}", opts.plugin.string());

        SqlConnection::SetDefaultConnectionString(SqlConnectionString {
            .value = std::format("DRIVER={};Database={}", SqliteDriver, opts.database.string()),
        });

        Tools::PluginLoader plugin { opts.plugin };
        auto const acquire = plugin.GetFunction<MigrationManager*()>("AcquireMigrationManager");
        if (!acquire)
            throw std::runtime_error("Plugin does not export AcquireMigrationManager");

        MigrationManager* const pluginManager = acquire();
        auto& manager = MigrationManager::GetInstance();
        if (pluginManager && pluginManager != &manager)
        {
            for (auto const& release: pluginManager->GetAllReleases())
                manager.RegisterRelease(release.version, release.highestTimestamp);
            for (auto const* migration: pluginManager->GetAllMigrations())
                manager.AddMigration(migration);
        }

        manager.CreateMigrationHistory();

        if (opts.apply)
        {
            auto const applied = manager.ApplyPendingMigrations();
            std::println();
            std::println("Applied {} migration(s).", applied);
        }

        auto const appliedIds = manager.GetAppliedMigrationIds();
        auto const& migrations = manager.GetAllMigrations();
        auto const& releases = manager.GetAllReleases();

        std::println();
        std::println("Registered migrations : {}", migrations.size());
        std::println("Registered releases   : {}", releases.size());
        std::println("Applied migrations    : {}", appliedIds.size());
        std::println();

        std::println("{:<16} {:<8} {}", "TIMESTAMP", "STATUS", "TITLE");
        std::println("{}", std::string(78, '-'));
        for (auto const* m: migrations)
        {
            auto const ts = m->GetTimestamp();
            auto const* const status = IsApplied(appliedIds, ts) ? "applied" : "pending";
            std::println("{:<16} {:<8} {}", ts.value, status, m->GetTitle());
        }

        std::println();
        std::println("{:<10} {}", "RELEASE", "HIGHEST TIMESTAMP");
        std::println("{}", std::string(35, '-'));
        for (auto const& r: releases)
            std::println("{:<10} {}", r.version, r.highestTimestamp.value);

        return 0;
    }
    catch (std::exception const& e)
    {
        std::println(std::cerr, "Error: {}", e.what());
        return 1;
    }
}
