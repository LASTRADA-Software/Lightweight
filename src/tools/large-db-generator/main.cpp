// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief CLI tool for generating large test databases.
///
/// This tool creates a complex 500MB+ test database that works across
/// all supported backends (SQLite3, PostgreSQL, MS-SQL Server).
///
/// Usage:
///   large-db-generator [options]
///
/// Options:
///   --connection-string <str>  ODBC connection string (default: SQLite3 file)
///   --size-mb <mb>             Target size in megabytes (default: 500)
///   --seed <seed>              Random seed for deterministic generation (default: 42)
///   --help                     Show this help message

#include "../../tests/LargeTestDatabase/DataGenerator.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>
#include <Lightweight/SqlConnection.hpp>

#include <cstdint>
#include <cstdlib>
#include <format>
#include <print>
#include <string>
#include <string_view>

using namespace Lightweight;
using namespace LargeDb;

namespace
{

struct CommandLineArgs
{
    std::string connectionString = "DRIVER=SQLite3;Database=large_test.db";
    size_t targetSizeMB = 500;
    uint64_t seed = 42;
    bool showHelp = false;
};

void PrintUsage(char const* programName)
{
    std::println("Usage: {} [options]", programName);
    std::println("");
    std::println("Creates a complex test database with 500MB+ of data for benchmarking");
    std::println("and integration testing. Works across all supported database backends.");
    std::println("");
    std::println("Options:");
    std::println("  --connection-string <str>  ODBC connection string");
    std::println("                             Default: DRIVER=SQLite3;Database=large_test.db");
    std::println("  --size-mb <mb>             Target size in megabytes (default: 500)");
    std::println("  --seed <seed>              Random seed for deterministic generation (default: 42)");
    std::println("  --help                     Show this help message");
    std::println("");
    std::println("Examples:");
    std::println("  # SQLite3 (default)");
    std::println("  {} --connection-string \"DRIVER=SQLite3;Database=test.db\"", programName);
    std::println("");
    std::println("  # PostgreSQL");
    std::println("  {} --connection-string \"DRIVER=PostgreSQL;Server=localhost;Database=testdb;Uid=user;Pwd=pass\"",
                 programName);
    std::println("");
    std::println("  # MS-SQL Server");
    std::println("  {} --connection-string \"DRIVER=ODBC Driver 18 for SQL "
                 "Server;Server=localhost;Database=testdb;Uid=user;Pwd=pass\"",
                 programName);
    std::println("");
    std::println("  # Generate smaller database (100MB)");
    std::println("  {} --size-mb 100", programName);
    std::println("");
    std::println("  # Reproducible generation with specific seed");
    std::println("  {} --seed 12345", programName);
}

CommandLineArgs ParseCommandLine(int argc, char* argv[])
{
    CommandLineArgs args;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            args.showHelp = true;
            return args;
        }
        else if (arg == "--connection-string" && i + 1 < argc)
        {
            args.connectionString = argv[++i];
        }
        else if (arg == "--size-mb" && i + 1 < argc)
        {
            args.targetSizeMB = static_cast<size_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--seed" && i + 1 < argc)
        {
            args.seed = std::stoull(argv[++i]);
        }
        else
        {
            std::println(stderr, "Unknown argument: {}", arg);
            args.showHelp = true;
            return args;
        }
    }

    return args;
}

double CalculateScaleFactor(size_t targetSizeMB)
{
    // Default config produces approximately 500MB
    constexpr size_t defaultSizeMB = 500;
    return static_cast<double>(targetSizeMB) / static_cast<double>(defaultSizeMB);
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    auto const args = ParseCommandLine(argc, argv);

    if (args.showHelp)
    {
        PrintUsage(argv[0]);
        return EXIT_SUCCESS;
    }

    std::println("Large Database Generator");
    std::println("========================");
    std::println("");
    std::println("Connection string: {}", SqlConnectionString::SanitizePwd(args.connectionString));
    std::println("Target size: {} MB", args.targetSizeMB);
    std::println("Random seed: {}", args.seed);
    std::println("");

    try
    {
        // Establish database connection
        SqlConnection::SetDefaultConnectionString(SqlConnectionString { args.connectionString });
        auto connection = SqlConnection();

        if (!connection.IsAlive())
        {
            std::println(stderr, "Failed to connect to database: {}", connection.LastError());
            return EXIT_FAILURE;
        }

        std::println("Connected to: {} ({})", connection.ServerName(), connection.ServerType());

        auto dm = DataMapper(std::move(connection));

        // Create schema
        std::println("");
        std::println("Creating database schema...");
        DropSchema(dm); // Clean up any existing tables
        CreateSchema(dm);
        std::println("Schema created successfully.");

        // Calculate scale factor and create config
        auto const scaleFactor = CalculateScaleFactor(args.targetSizeMB);
        auto config = CreateScaledConfig(scaleFactor);
        config.seed = args.seed;

        // Calculate expected size
        auto const expectedSize = GetExpectedDataSize(config);
        std::println("");
        std::println("Expected data size: {} MB ({} bytes)", expectedSize / (1024 * 1024), expectedSize);

        // Populate database with progress reporting
        std::println("");
        std::println("Populating database...");

        PopulateDatabase(dm, config, [](double progress, std::string_view entity) {
            std::println("  [{:3.0f}%] Created {}", progress * 100, entity);
        });

        std::println("");
        std::println("Database population complete!");

        // Print summary
        std::println("");
        std::println("Summary:");
        std::println("  Users:           {}", config.userCount);
        std::println("  Categories:      {}", config.categoryCount);
        std::println("  Products:        {}", config.productCount);
        std::println("  Product Images:  {}", config.productImageCount);
        std::println("  Orders:          {}", config.orderCount);
        std::println("  Order Items:     {}", config.orderItemCount);
        std::println("  Reviews:         {}", config.reviewCount);
        std::println("  Tags:            {}", config.tagCount);
        std::println("  Product Tags:    {}", config.productTagCount);
        std::println("  Activity Logs:   {}", config.activityLogCount);
        std::println("  System Audit:    {}", config.systemAuditLogCount);
        std::println("  Articles:        {}", config.articleCount);

        return 0;
    }
    catch (SqlException const& e)
    {
        std::println(stderr, "SQL Error: {}", e.what());
        return EXIT_FAILURE;
    }
    catch (std::exception const& e)
    {
        std::println(stderr, "Error: {}", e.what());
        return EXIT_FAILURE;
    }
}
