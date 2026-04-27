// SPDX-License-Identifier: Apache-2.0

#include "CodeGenerator.hpp"
#include "LupSqlParser.hpp"
#include "SqlStatementParser.hpp"

#include <Lightweight/CodeGen/SplitFileWriter.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <span>
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
    std::println(stderr, "  --emit-cmake            Also write CMakeLists.txt + Plugin.cpp next to");
    std::println(stderr, "                          the generated migration sources so the output");
    std::println(stderr, "                          directory is a drop-in plugin subdirectory.");
    std::println(stderr, "                          (only meaningful with a multi-file --output pattern)");
    std::println(stderr, "  --plugin-name <NAME>    CMake target name for the emitted plugin.");
    std::println(stderr, "                          (default: LupMigrations)");
    std::println(stderr, "  --max-lines-per-file N  Split any migration whose generated code would exceed");
    std::println(stderr, "                          N lines across additional `<basename>_partNN.cpp` files.");
    std::println(stderr, "                          Use 0 to disable splitting. (default: 5000)");
    std::println(stderr, "  --varchar-scale N       Multiply every parameterised character-column size by");
    std::println(stderr, "                          N (e.g. VARCHAR(100) -> VARCHAR(N*100)). Default 1.");
    std::println(stderr, "                          Set N=4 when targeting Unicode-counting backends like");
    std::println(stderr, "                          PostgreSQL where multi-byte data needs more headroom.");
    std::println(stderr, "  --force-unicode         Widen CHAR/VARCHAR columns to NCHAR/NVARCHAR in the");
    std::println(stderr, "                          generated DSL. This is the default: it keeps MSSQL");
    std::println(stderr, "                          char-counted so multi-byte source data (e.g. German");
    std::println(stderr, "                          umlauts) does not overflow byte-counted VARCHAR");
    std::println(stderr, "                          columns. Semantic no-op on SQLite/PostgreSQL.");
    std::println(stderr, "                          Accepted for backwards compatibility; has no effect");
    std::println(stderr, "                          unless `--no-force-unicode` was passed earlier.");
    std::println(stderr, "  --no-force-unicode      Opt out of Unicode widening; emit CHAR/VARCHAR as-is.");
    std::println(stderr, "  --help                  Show this help message");
    std::println(stderr, "");
    std::println(stderr, "Examples:");
    std::println(stderr, "  # Single file output (all migrations in one file)");
    std::println(stderr, "  {} --input-dir ./lup-sql --output ./GeneratedMigrations.cpp", programName);
    std::println(stderr, "");
    std::println(stderr, "  # Multi-file output (one file per migration)");
    std::println(stderr, R"(  {} --input-dir ./lup-sql --output "./lup_{{version}}.cpp")", programName);
    std::println(stderr, "");
    std::println(stderr, "  # Multi-file output + drop-in plugin CMake build");
    std::println(stderr, R"(  {} --input-dir ./lup-sql --output "./gen/lup_{{version}}.cpp" --emit-cmake)", programName);
    std::println(stderr, "");
}

struct Arguments
{
    std::filesystem::path inputDir;
    std::string outputPattern;
    std::string inputEncoding = "windows-1252";
    std::string pluginName = "LupMigrations";
    bool emitCmake = false;
    bool showHelp = false;
    /// Threshold for auto-splitting a generated migration into multiple `.cpp` files.
    /// Zero means "never split". Huge migrations (e.g. the LASTRADA `2_3_6` bundle)
    /// produced a ~34k-line TU that exhausted memory in debug+ASan builds — the
    /// split puts each part function in its own translation unit.
    size_t maxLinesPerFile = 5000;

    /// When true, widen every byte-char column type to its Unicode variant. See
    /// `CodeGeneratorConfig::forceUnicode` for rationale. Defaults to true — the
    /// backend formatters downgrade back to `CHAR`/`VARCHAR` on SQLite/PostgreSQL,
    /// so this is a semantic no-op there, while MSSQL gets char-counted widths.
    bool forceUnicode = true;

    /// Multiplier for `varchar(N)` / `char(N)` sizes. See `CodeGeneratorConfig::varcharScale`.
    int varcharScale = 1;
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

bool ParseMaxLinesPerFileArg(int& i, int argc, char* argv[], Arguments& args);
bool ParseVarcharScaleArg(int& i, int argc, char* argv[], Arguments& args);

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

    if (std::strcmp(argv[i], "--emit-cmake") == 0)
    {
        args.emitCmake = true;
        return true;
    }

    if (std::strcmp(argv[i], "--plugin-name") == 0)
    {
        if (i + 1 >= argc)
        {
            std::println(stderr, "Error: --plugin-name requires an argument");
            return false;
        }
        args.pluginName = argv[++i];
        return true;
    }

    if (std::strcmp(argv[i], "--force-unicode") == 0)
    {
        args.forceUnicode = true;
        return true;
    }

    if (std::strcmp(argv[i], "--no-force-unicode") == 0)
    {
        args.forceUnicode = false;
        return true;
    }

    if (std::strcmp(argv[i], "--varchar-scale") == 0)
        return ParseVarcharScaleArg(i, argc, argv, args);

    if (std::strcmp(argv[i], "--max-lines-per-file") == 0)
        return ParseMaxLinesPerFileArg(i, argc, argv, args);

    std::println(stderr, "Error: Unknown option: {}", argv[i]);
    return false;
}

/// Handles `--varchar-scale N`. Extracted from `ParseSingleArg` to keep its
/// cognitive complexity under the project-wide clang-tidy threshold.
bool ParseVarcharScaleArg(int& i, int argc, char* argv[], Arguments& args)
{
    if (i + 1 >= argc)
    {
        std::println(stderr, "Error: --varchar-scale requires an argument");
        return false;
    }
    try
    {
        auto const parsed = std::stoi(argv[++i]);
        if (parsed < 1)
        {
            std::println(stderr, "Error: --varchar-scale must be >= 1");
            return false;
        }
        args.varcharScale = parsed;
    }
    catch (std::exception const&)
    {
        std::println(stderr, "Error: --varchar-scale expects an integer");
        return false;
    }
    return true;
}

/// Handles `--max-lines-per-file N`. Extracted from `ParseSingleArg` to keep its
/// cognitive complexity under the project-wide clang-tidy threshold.
bool ParseMaxLinesPerFileArg(int& i, int argc, char* argv[], Arguments& args)
{
    if (i + 1 >= argc)
    {
        std::println(stderr, "Error: --max-lines-per-file requires an argument");
        return false;
    }
    try
    {
        auto const parsed = std::stoll(argv[++i]);
        if (parsed < 0)
        {
            std::println(stderr, "Error: --max-lines-per-file must be >= 0");
            return false;
        }
        args.maxLinesPerFile = static_cast<size_t>(parsed);
    }
    catch (std::exception const&)
    {
        std::println(stderr, "Error: --max-lines-per-file expects an integer");
        return false;
    }
    return true;
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

/// @brief Validates that all statements were parsed structurally.
///
/// The generator never emits `plan.RawSql(...)` — every input must fit one of the
/// structured builder calls (CreateTable/AlterTable/Insert/Update/Delete/…). If a
/// statement remains as `RawSqlStmt` it means the parser needs extending for that
/// SQL form.
bool ValidateParsedMigrations(std::vector<Lup2DbTool::ParsedMigration> const& migrations)
{
    bool allValid = true;
    for (auto const& migration: migrations)
    {
        for (auto const& stmtWithComments: migration.statements)
        {
            if (!std::holds_alternative<Lup2DbTool::RawSqlStmt>(stmtWithComments.statement))
                continue;
            auto const& raw = std::get<Lup2DbTool::RawSqlStmt>(stmtWithComments.statement);
            std::println(stderr, "Error: Unparseable SQL in {}:", migration.sourceFile.filename().string());
            auto preview = raw.sql.substr(0, 200);
            if (raw.sql.size() > 200)
                preview += "...";
            std::println(stderr, "  {}", preview);
            allValid = false;
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

size_t CountLines(std::string_view s) noexcept
{
    return static_cast<size_t>(std::count(s.begin(), s.end(), '\n'));
}

/// @brief Returns the base output path without the `.cpp` (or other) extension.
/// Used to derive `<base>_partNN.cpp` sibling filenames for split migrations.
std::string StripCppExtension(std::string const& path)
{
    constexpr std::string_view ext { ".cpp" };
    if (path.size() > ext.size() && std::string_view { path }.ends_with(ext))
        return path.substr(0, path.size() - ext.size());
    return path;
}

/// @brief Stable C++ identifier suffix for a migration's split parts.
/// Uses the numeric migration timestamp to guarantee uniqueness across the whole plugin,
/// even when two migrations share a base filename (never the case today, but cheap to
/// guard against).
std::string PartFunctionBasename(Lup2DbTool::ParsedMigration const& migration)
{
    return std::format("LupMigration_{}_Part",
                       migration.targetVersion.ToMigrationTimestamp());
}

/// Renders the main (coordinator) file for a split migration — contains the
/// `LIGHTWEIGHT_SQL_MIGRATION` macro, forward declarations of each part, and a body
/// that simply calls them in order.
void WriteSplitMainFile(std::ofstream& out,
                        Lup2DbTool::CodeGenerator const& generator,
                        Lup2DbTool::ParsedMigration const& migration,
                        size_t numParts)
{
    generator.GenerateFileHeader(out);
    for (size_t i = 1; i <= numParts; ++i)
        out << "void " << PartFunctionBasename(migration) << i
            << "(SqlMigrationQueryBuilder& plan);\n";
    out << "\n";
    generator.WriteMigrationHeaderComment(migration, out);

    auto const timestamp = migration.targetVersion.ToMigrationTimestamp();
    out << "LIGHTWEIGHT_SQL_MIGRATION(" << timestamp << ", \""
        << Lup2DbTool::CodeGenerator::EscapeForCppStringLiteral(migration.title)
        << "\")\n"
        << "{\n";
    for (size_t i = 1; i <= numParts; ++i)
        out << "    " << PartFunctionBasename(migration) << i << "(plan);\n";
    out << "}\n\n";
    generator.WriteReleaseMarker(migration, out);
    generator.GenerateFileFooter(out);
}

/// Ensures the parent directory of `filePath` exists so a subsequent `std::ofstream`
/// open does not fail with ENOENT when the caller points at a brand-new subtree.
/// Paths with no parent component (bare filenames) are a no-op.
bool EnsureParentDirectoryExists(std::filesystem::path const& filePath)
{
    auto const parent = filePath.parent_path();
    if (parent.empty())
        return true;

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec)
    {
        std::println(stderr, "Error: Failed to create directory {}: {}", parent.string(), ec.message());
        return false;
    }
    return true;
}

/// Writes a single part file containing one `LupMigration_<ts>_PartN` function whose
/// body is the concatenation of the supplied pre-rendered statement blocks.
bool WriteSplitPartFile(std::string const& partPath,
                        Lup2DbTool::CodeGenerator const& generator,
                        Lup2DbTool::ParsedMigration const& migration,
                        size_t partIndex,
                        std::span<std::string const> blocks)
{
    if (!EnsureParentDirectoryExists(partPath))
        return false;
    std::ofstream out(partPath);
    if (!out.is_open())
    {
        std::println(stderr, "Error: Failed to open output file: {}", partPath);
        return false;
    }
    generator.GenerateFileHeader(out);
    out << "void " << PartFunctionBasename(migration) << partIndex
        << "(SqlMigrationQueryBuilder& plan)\n"
        << "{\n";
    for (auto const& block: blocks)
        out << block;
    out << "}\n\n";
    generator.GenerateFileFooter(out);
    return true;
}

/// @brief Groups statement blocks into chunks, each at most `maxLines` lines
/// (the last chunk may be smaller). A single statement larger than the
/// threshold gets its own oversize chunk — we never split mid-statement, since
/// statements are chained builder calls that cannot be broken across TUs.
///
/// Thin wrapper that adapts lup2dbtool's `vector<string>` block representation to
/// the shared `Lightweight::CodeGen::CodeBlock` format and back. The bin-packing
/// itself lives in `Lightweight::CodeGen::GroupBlocksByLineBudget` so dbtool's
/// fold emitter can reuse the same logic.
std::vector<std::vector<std::string>> GroupBlocksByLineBudget(
    std::vector<std::string> const& blocks, size_t maxLines)
{
    std::vector<Lightweight::CodeGen::CodeBlock> codeBlocks;
    codeBlocks.reserve(blocks.size());
    for (auto const& b: blocks)
        codeBlocks.push_back(Lightweight::CodeGen::CodeBlock { .content = b, .lineCount = CountLines(b) });
    auto chunks = Lightweight::CodeGen::GroupBlocksByLineBudget(codeBlocks, maxLines);
    std::vector<std::vector<std::string>> result;
    result.reserve(chunks.size());
    for (auto& chunk: chunks)
    {
        std::vector<std::string> stringChunk;
        stringChunk.reserve(chunk.size());
        for (auto& block: chunk)
            stringChunk.push_back(std::move(block.content));
        result.push_back(std::move(stringChunk));
    }
    return result;
}

/// @brief Writes one migration as a single file (no split).
bool WriteSingleMigrationFile(std::string const& outputPath,
                              Lup2DbTool::CodeGenerator& generator,
                              Lup2DbTool::ParsedMigration const& migration,
                              std::vector<Lup2DbTool::CodeGeneratorDiagnostic>& diagnostics)
{
    if (!EnsureParentDirectoryExists(outputPath))
        return false;
    std::ofstream out(outputPath);
    if (!out.is_open())
    {
        std::println(stderr, "Error: Failed to open output file: {}", outputPath);
        return false;
    }
    generator.GenerateFileHeader(out);
    generator.GenerateMigration(migration, out, diagnostics);
    generator.GenerateFileFooter(out);
    return true;
}

/// @brief Decides whether a migration needs splitting and emits the resulting files.
///
/// `maxLinesPerFile == 0` disables splitting entirely. Otherwise the function renders
/// every statement to its own buffer, sums the line counts, and if the total (plus a
/// small fixed overhead for the boilerplate header/footer) exceeds the threshold it
/// writes a coordinator `<basename>.cpp` plus `<basename>_partNN.cpp` sibling files.
bool GenerateOneMigration(std::string const& outputPath,
                          Lup2DbTool::CodeGenerator& generator,
                          Lup2DbTool::ParsedMigration const& migration,
                          size_t maxLinesPerFile,
                          std::vector<Lup2DbTool::CodeGeneratorDiagnostic>& diagnostics)
{
    if (maxLinesPerFile == 0)
    {
        std::println(stderr, "Generating: {}", outputPath);
        return WriteSingleMigrationFile(outputPath, generator, migration, diagnostics);
    }

    auto const blocks = generator.GenerateStatementBlocks(migration, diagnostics);

    size_t totalLines = 0;
    for (auto const& block: blocks)
        totalLines += CountLines(block);

    if (totalLines <= maxLinesPerFile)
    {
        std::println(stderr, "Generating: {}", outputPath);
        return WriteSingleMigrationFile(outputPath, generator, migration, diagnostics);
    }

    auto const chunks = GroupBlocksByLineBudget(blocks, maxLinesPerFile);
    auto const basename = StripCppExtension(outputPath);

    std::println(stderr,
                 "Generating: {} (+ {} part file(s); {} lines > {} threshold)",
                 outputPath, chunks.size(), totalLines, maxLinesPerFile);

    for (size_t i = 0; i < chunks.size(); ++i)
    {
        auto const partPath = std::format("{}_part{:02}.cpp", basename, i + 1);
        if (!WriteSplitPartFile(partPath, generator, migration, i + 1, chunks[i]))
            return false;
    }

    if (!EnsureParentDirectoryExists(outputPath))
        return false;
    std::ofstream mainOut(outputPath);
    if (!mainOut.is_open())
    {
        std::println(stderr, "Error: Failed to open output file: {}", outputPath);
        return false;
    }
    WriteSplitMainFile(mainOut, generator, migration, chunks.size());
    return true;
}

/// @brief Generates multi-file output (one coordinator + optional part files per migration).
/// @return true on success, false on failure.
bool GenerateMultiFileOutput(std::vector<Lup2DbTool::ParsedMigration> const& migrations,
                             std::string const& outputPattern,
                             size_t maxLinesPerFile,
                             Lup2DbTool::CodeGenerator& generator,
                             std::vector<Lup2DbTool::CodeGeneratorDiagnostic>& diagnostics)
{
    for (auto const& migration: migrations)
    {
        auto outputPath = Lup2DbTool::ResolveOutputPattern(outputPattern, migration.targetVersion);
        if (!GenerateOneMigration(outputPath, generator, migration, maxLinesPerFile, diagnostics))
            return false;
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

    if (!EnsureParentDirectoryExists(outputPath))
        return false;
    std::ofstream out(outputPath);
    if (!out.is_open())
    {
        std::println(stderr, "Error: Failed to open output file: {}", outputPath);
        return false;
    }

    generator.GenerateAllMigrations(migrations, out, diagnostics);
    return true;
}

/// @brief Writes a CMakeLists.txt and a Plugin.cpp next to the generated migration sources
/// so the output directory becomes a drop-in migration plugin that a parent project can
/// consume via `add_subdirectory()`.
/// @return true on success, false if either file could not be written.
bool EmitPluginFiles(std::filesystem::path const& outputDir, std::string const& pluginName)
{
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec)
    {
        std::println(stderr, "Error: Failed to create directory {}: {}", outputDir.string(), ec.message());
        return false;
    }

    auto const cmakePath = outputDir / "CMakeLists.txt";
    std::println(stderr, "Generating: {}", cmakePath.string());
    std::ofstream cmakeOut(cmakePath);
    if (!cmakeOut.is_open())
    {
        std::println(stderr, "Error: Failed to open output file: {}", cmakePath.string());
        return false;
    }
    Lup2DbTool::CodeGenerator::GeneratePluginCMake(cmakeOut, pluginName);

    auto const pluginPath = outputDir / "Plugin.cpp";
    std::println(stderr, "Generating: {}", pluginPath.string());
    std::ofstream pluginOut(pluginPath);
    if (!pluginOut.is_open())
    {
        std::println(stderr, "Error: Failed to open output file: {}", pluginPath.string());
        return false;
    }
    Lup2DbTool::CodeGenerator::GeneratePluginEntryPoint(pluginOut);
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

    // Substitute real column names for positional INSERT indices emitted by
    // ParseInsert when the source SQL was `INSERT INTO T VALUES (...)` without
    // an explicit column list. See ResolvePositionalInserts() for details.
    Lup2DbTool::ResolvePositionalInserts(*migrations);

    Lup2DbTool::CodeGeneratorConfig generatorConfig;
    generatorConfig.forceUnicode = args.forceUnicode;
    generatorConfig.varcharScale = args.varcharScale;
    Lup2DbTool::CodeGenerator generator { std::move(generatorConfig) };
    std::vector<Lup2DbTool::CodeGeneratorDiagnostic> diagnostics;

    bool const multiFile = Lup2DbTool::IsMultiFilePattern(args.outputPattern);
    bool success = multiFile ? GenerateMultiFileOutput(*migrations, args.outputPattern, args.maxLinesPerFile, generator, diagnostics)
                             : GenerateSingleFileOutput(*migrations, args.outputPattern, generator, diagnostics);

    // Report any diagnostics (warnings/errors from code generation)
    ReportDiagnostics(diagnostics);

    if (!success)
        return 1;

    if (args.emitCmake)
    {
        if (!multiFile)
        {
            std::println(stderr,
                         "Warning: --emit-cmake requires a multi-file --output pattern "
                         "(e.g. containing '{{version}}'); skipping plugin emission.");
        }
        else if (!EmitPluginFiles(std::filesystem::path(args.outputPattern).parent_path(), args.pluginName))
        {
            return 1;
        }
    }

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
