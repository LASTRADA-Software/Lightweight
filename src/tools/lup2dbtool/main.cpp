// SPDX-License-Identifier: Apache-2.0

#include "CodeGenerator.hpp"
#include "LupSqlParser.hpp"
#include "SqlStatementParser.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <print>
#include <variant>

namespace
{

void PrintUsage(char const* programName)
{
    std::println(stderr, "Usage: {} [OPTIONS]", programName);
    std::println(stderr, "");
    std::println(stderr, "Parse LUP SQL migration files and generate C++ migration code.");
    std::println(stderr, "");
    std::println(stderr, "Options:");
    std::println(stderr, "  --input-dir <DIR>       Directory containing SQL migration files (required)");
    std::println(stderr, "  --output <FILE>         Output file path (required)");
    std::println(stderr, "                          Supports pattern substitution for multi-file mode:");
    std::println(stderr, "                          {{major}}, {{minor}}, {{patch}}, {{version}}");
    std::println(stderr, "  --input-encoding <ENC>  Input file encoding: windows-1252 or utf-8");
    std::println(stderr, "                          (default: windows-1252)");
    std::println(stderr, "  --help                  Show this help message");
    std::println(stderr, "");
    std::println(stderr, "Examples:");
    std::println(stderr, "  # Single file output (all migrations in one file)");
    std::println(stderr, "  {} --input-dir ./model4_JP --output ./GeneratedMigrations.cpp", programName);
    std::println(stderr, "");
    std::println(stderr, "  # Multi-file output (one file per migration)");
    std::println(stderr, R"(  {} --input-dir ./model4_JP --output "./lup_{{version}}.cpp")", programName);
    std::println(stderr, "");
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
        std::println(stderr, "Error: --input-dir is required");
        return false;
    }
    if (args.outputPattern.empty())
    {
        std::println(stderr, "Error: --output is required");
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
            std::println(stderr, "Error: --input-dir requires an argument");
            return false;
        }
        args.inputDir = argv[++i];
        return true;
    }

    if (std::strcmp(argv[i], "--output") == 0)
    {
        if (i + 1 >= argc)
        {
            std::println(stderr, "Error: --output requires an argument");
            return false;
        }
        args.outputPattern = argv[++i];
        return true;
    }

    if (std::strcmp(argv[i], "--input-encoding") == 0)
    {
        if (i + 1 >= argc)
        {
            std::println(stderr, "Error: --input-encoding requires an argument");
            return false;
        }
        args.inputEncoding = argv[++i];
        if (args.inputEncoding != "windows-1252" && args.inputEncoding != "utf-8")
        {
            std::println(stderr, "Error: Invalid encoding. Use 'windows-1252' or 'utf-8'");
            return false;
        }
        return true;
    }

    std::println(stderr, "Error: Unknown option: {}", argv[i]);
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

/// @brief Reports diagnostics from code generation.
void ReportDiagnostics(std::vector<Lup2DbTool::CodeGeneratorDiagnostic> const& diagnostics)
{
    for (auto const& diag: diagnostics)
    {
        char const* severityStr = diag.severity == Lup2DbTool::DiagnosticSeverity::Warning ? "Warning" : "Error";
        if (diag.columnName.empty())
        {
            std::println(stderr, "{}: [{}] {}", severityStr, diag.tableName, diag.message);
        }
        else
        {
            std::println(stderr, "{}: [{}::{}] {}", severityStr, diag.tableName, diag.columnName, diag.message);
        }
    }
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
                std::println(stderr, "Error: Unparseable SQL in {}:", migration.sourceFile.filename().string());
                auto preview = raw.sql.substr(0, 200);
                if (raw.sql.size() > 200)
                    preview += "...";
                std::println(stderr, "  {}", preview);
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
        std::println(stderr, "Error: Input directory does not exist: {}", inputDir.string());
        return false;
    }
    if (!std::filesystem::is_directory(inputDir))
    {
        std::println(stderr, "Error: Not a directory: {}", inputDir.string());
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
        std::println(stderr, "Parsing: {}", file.filename().string());
        auto migration = Lup2DbTool::ParseSqlFile(file, config);
        if (!migration)
        {
            std::println(stderr, "Error: Failed to parse {}", file.string());
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
                             Lup2DbTool::CodeGenerator& generator,
                             std::vector<Lup2DbTool::CodeGeneratorDiagnostic>& diagnostics)
{
    for (auto const& migration: migrations)
    {
        auto outputPath = Lup2DbTool::ResolveOutputPattern(outputPattern, migration.targetVersion);
        std::println(stderr, "Generating: {}", outputPath);

        std::ofstream out(outputPath);
        if (!out.is_open())
        {
            std::println(stderr, "Error: Failed to open output file: {}", outputPath);
            return false;
        }

        generator.GenerateFileHeader(out);
        generator.GenerateMigration(migration, out, diagnostics);
        generator.GenerateFileFooter(out);
    }
    return true;
}

/// @brief Generates single-file output (all migrations in one file).
/// @return true on success, false on failure.
bool GenerateSingleFileOutput(std::vector<Lup2DbTool::ParsedMigration> const& migrations,
                              std::string const& outputPath,
                              Lup2DbTool::CodeGenerator& generator,
                              std::vector<Lup2DbTool::CodeGeneratorDiagnostic>& diagnostics)
{
    std::println(stderr, "Generating: {}", outputPath);

    std::ofstream out(outputPath);
    if (!out.is_open())
    {
        std::println(stderr, "Error: Failed to open output file: {}", outputPath);
        return false;
    }

    generator.GenerateAllMigrations(migrations, out, diagnostics);
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    Arguments args;
    if (!ParseArguments(argc, argv, args))
    {
        std::println(stderr, "\nUse --help for usage information.");
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
        std::println(stderr, "Warning: No migration files found in {}", args.inputDir.string());
        return 0;
    }

    std::println(stderr, "Found {} migration file(s)", files.size());

    Lup2DbTool::ParserConfig parserConfig;
    parserConfig.inputEncoding = args.inputEncoding;

    auto migrations = ParseMigrationFiles(files, parserConfig);
    if (!migrations)
        return 1;

    if (!ValidateParsedMigrations(*migrations))
    {
        std::println(stderr, "Error: Some SQL statements could not be parsed.");
        std::println(stderr, "Please extend the SQL parser to handle these statement types.");
        return 1;
    }

    Lup2DbTool::CodeGenerator generator;
    std::vector<Lup2DbTool::CodeGeneratorDiagnostic> diagnostics;

    bool success = Lup2DbTool::IsMultiFilePattern(args.outputPattern)
                       ? GenerateMultiFileOutput(*migrations, args.outputPattern, generator, diagnostics)
                       : GenerateSingleFileOutput(*migrations, args.outputPattern, generator, diagnostics);

    // Report any diagnostics (warnings/errors from code generation)
    ReportDiagnostics(diagnostics);

    if (!success)
        return 1;

    // Check if there were any warnings
    bool hasWarnings = std::ranges::any_of(
        diagnostics, [](auto const& d) { return d.severity == Lup2DbTool::DiagnosticSeverity::Warning; });

    if (hasWarnings)
    {
        std::println(stderr, "Generated {} migration(s) with warnings.", migrations->size());
    }
    else
    {
        std::println(stderr, "Done. Generated {} migration(s).", migrations->size());
    }

    return 0;
}
