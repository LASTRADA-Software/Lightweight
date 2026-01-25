// SPDX-License-Identifier: Apache-2.0

#include "CodeGenerator.hpp"
#include "LupSqlParser.hpp"
#include "SqlStatementParser.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <variant>

namespace
{

void PrintUsage(char const* programName)
{
    std::cerr << "Usage: " << programName << " [OPTIONS]\n"
              << "\n"
              << "Parse LUP SQL migration files and generate C++ migration code.\n"
              << "\n"
              << "Options:\n"
              << "  --input-dir <DIR>       Directory containing SQL migration files (required)\n"
              << "  --output <FILE>         Output file path (required)\n"
              << "                          Supports pattern substitution for multi-file mode:\n"
              << "                          {major}, {minor}, {patch}, {version}\n"
              << "  --input-encoding <ENC>  Input file encoding: windows-1252 or utf-8\n"
              << "                          (default: windows-1252)\n"
              << "  --help                  Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  # Single file output (all migrations in one file)\n"
              << "  " << programName << " --input-dir ./model4_JP --output ./GeneratedMigrations.cpp\n"
              << "\n"
              << "  # Multi-file output (one file per migration)\n"
              << "  " << programName << " --input-dir ./model4_JP --output \"./lup_{version}.cpp\"\n"
              << "\n";
}

struct Arguments
{
    std::filesystem::path inputDir;
    std::string outputPattern;
    std::string inputEncoding = "windows-1252";
    bool showHelp = false;
};

bool ValidateRequiredArgs(Arguments const& args)
{
    if (args.inputDir.empty())
    {
        std::cerr << "Error: --input-dir is required\n";
        return false;
    }
    if (args.outputPattern.empty())
    {
        std::cerr << "Error: --output is required\n";
        return false;
    }
    return true;
}

bool ParseSingleArg(int& i, int argc, char* argv[], Arguments& args)
{
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
    {
        args.showHelp = true;
        return true;
    }

    if (std::strcmp(argv[i], "--input-dir") == 0)
    {
        if (i + 1 >= argc)
        {
            std::cerr << "Error: --input-dir requires an argument\n";
            return false;
        }
        args.inputDir = argv[++i];
        return true;
    }

    if (std::strcmp(argv[i], "--output") == 0)
    {
        if (i + 1 >= argc)
        {
            std::cerr << "Error: --output requires an argument\n";
            return false;
        }
        args.outputPattern = argv[++i];
        return true;
    }

    if (std::strcmp(argv[i], "--input-encoding") == 0)
    {
        if (i + 1 >= argc)
        {
            std::cerr << "Error: --input-encoding requires an argument\n";
            return false;
        }
        args.inputEncoding = argv[++i];
        if (args.inputEncoding != "windows-1252" && args.inputEncoding != "utf-8")
        {
            std::cerr << "Error: Invalid encoding. Use 'windows-1252' or 'utf-8'\n";
            return false;
        }
        return true;
    }

    std::cerr << "Error: Unknown option: " << argv[i] << "\n";
    return false;
}

bool ParseArguments(int argc, char* argv[], Arguments& args)
{
    for (int i = 1; i < argc; ++i)
    {
        if (!ParseSingleArg(i, argc, argv, args))
            return false;
        if (args.showHelp)
            return true;
    }

    return args.showHelp || ValidateRequiredArgs(args);
}

/// @brief Validates that all statements were parsed successfully (no RawSqlStmt).
/// @return true if all statements are valid, false if any RawSqlStmt was found.
bool ValidateParsedMigrations(std::vector<Lup2DbTool::ParsedMigration> const& migrations)
{
    bool allValid = true;
    for (auto const& migration: migrations)
    {
        for (auto const& stmtWithComments: migration.statements)
        {
            if (std::holds_alternative<Lup2DbTool::RawSqlStmt>(stmtWithComments.statement))
            {
                auto const& raw = std::get<Lup2DbTool::RawSqlStmt>(stmtWithComments.statement);
                std::cerr << "Error: Unparseable SQL in " << migration.sourceFile.filename().string() << ":\n";
                std::cerr << "  " << raw.sql.substr(0, 200);
                if (raw.sql.size() > 200)
                    std::cerr << "...";
                std::cerr << "\n";
                allValid = false;
            }
        }
    }
    return allValid;
}

/// @brief Validates the input directory exists and is a directory.
/// @return true if valid, false otherwise.
bool ValidateInputDirectory(std::filesystem::path const& inputDir)
{
    if (!std::filesystem::exists(inputDir))
    {
        std::cerr << "Error: Input directory does not exist: " << inputDir << "\n";
        return false;
    }
    if (!std::filesystem::is_directory(inputDir))
    {
        std::cerr << "Error: Not a directory: " << inputDir << "\n";
        return false;
    }
    return true;
}

/// @brief Parses all migration files in the given list.
/// @return The parsed migrations, or nullopt on failure.
std::optional<std::vector<Lup2DbTool::ParsedMigration>> ParseMigrationFiles(std::vector<std::filesystem::path> const& files,
                                                                            Lup2DbTool::ParserConfig const& config)
{
    std::vector<Lup2DbTool::ParsedMigration> migrations;
    for (auto const& file: files)
    {
        std::cerr << "Parsing: " << file.filename().string() << "\n";
        auto migration = Lup2DbTool::ParseSqlFile(file, config);
        if (!migration)
        {
            std::cerr << "Error: Failed to parse " << file << "\n";
            return std::nullopt;
        }
        migrations.push_back(std::move(*migration));
    }
    return migrations;
}

/// @brief Generates multi-file output (one file per migration).
/// @return true on success, false on failure.
bool GenerateMultiFileOutput(std::vector<Lup2DbTool::ParsedMigration> const& migrations,
                             std::string const& outputPattern,
                             Lup2DbTool::CodeGenerator& generator)
{
    for (auto const& migration: migrations)
    {
        auto outputPath = Lup2DbTool::ResolveOutputPattern(outputPattern, migration.targetVersion);
        std::cerr << "Generating: " << outputPath << "\n";

        std::ofstream out(outputPath);
        if (!out.is_open())
        {
            std::cerr << "Error: Failed to open output file: " << outputPath << "\n";
            return false;
        }

        generator.WriteFileHeader(out);
        generator.GenerateMigration(migration, out);
        generator.WriteFileFooter(out);
    }
    return true;
}

/// @brief Generates single-file output (all migrations in one file).
/// @return true on success, false on failure.
bool GenerateSingleFileOutput(std::vector<Lup2DbTool::ParsedMigration> const& migrations,
                              std::string const& outputPath,
                              Lup2DbTool::CodeGenerator& generator)
{
    std::cerr << "Generating: " << outputPath << "\n";

    std::ofstream out(outputPath);
    if (!out.is_open())
    {
        std::cerr << "Error: Failed to open output file: " << outputPath << "\n";
        return false;
    }

    generator.GenerateAllMigrations(migrations, out);
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    Arguments args;
    if (!ParseArguments(argc, argv, args))
    {
        std::cerr << "\nUse --help for usage information.\n";
        return 1;
    }

    if (args.showHelp)
    {
        PrintUsage(argv[0]);
        return 0;
    }

    if (!ValidateInputDirectory(args.inputDir))
        return 1;

    auto files = Lup2DbTool::DiscoverMigrationFiles(args.inputDir);
    if (files.empty())
    {
        std::cerr << "Warning: No migration files found in " << args.inputDir << "\n";
        return 0;
    }

    std::cerr << "Found " << files.size() << " migration file(s)\n";

    Lup2DbTool::ParserConfig parserConfig;
    parserConfig.inputEncoding = args.inputEncoding;

    auto migrations = ParseMigrationFiles(files, parserConfig);
    if (!migrations)
        return 1;

    if (!ValidateParsedMigrations(*migrations))
    {
        std::cerr << "Error: Some SQL statements could not be parsed.\n";
        std::cerr << "Please extend the SQL parser to handle these statement types.\n";
        return 1;
    }

    Lup2DbTool::CodeGenerator generator;

    bool success = Lup2DbTool::IsMultiFilePattern(args.outputPattern)
                       ? GenerateMultiFileOutput(*migrations, args.outputPattern, generator)
                       : GenerateSingleFileOutput(*migrations, args.outputPattern, generator);

    if (!success)
        return 1;

    std::cerr << "Done. Generated " << migrations->size() << " migration(s).\n";
    return 0;
}
