// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "LupSqlParser.hpp"

#include <expected>
#include <filesystem>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

namespace Lup2DbTool
{

/// @brief Severity level for code generation diagnostics.
enum class DiagnosticSeverity : std::uint8_t
{
    Warning, ///< Non-fatal issue, code generated with fallback
    Error    ///< Fatal issue, code generation may be incomplete
};

/// @brief Diagnostic message from code generation.
struct CodeGeneratorDiagnostic
{
    DiagnosticSeverity severity;
    std::string tableName;
    std::string columnName;
    std::string message;
};

/// @brief Configuration for C++ code generation.
struct CodeGeneratorConfig
{
    /// Namespace for generated migrations (default: "Lup::Migrations")
    std::string namespacePrefix = "Lup::Migrations";

    /// Include guard prefix (default: "LUP_MIGRATIONS_")
    std::string includeGuardPrefix = "LUP_MIGRATIONS_";

    /// @brief When set, widen every byte-char column type to its Unicode counterpart:
    /// `CHAR(n)` → `NChar(n)`, `VARCHAR(n)` → `NVarchar(n)`.
    ///
    /// The LUP source files are Windows-1252 encoded — `VARCHAR(n)` means *n bytes of
    /// codepage text*. That's fine when the target DB also uses codepage VARCHARs, but
    /// sending UTF-8 data into MSSQL `VARCHAR` causes truncation on multi-byte
    /// characters (German umlauts, …). Enabling this option emits `NVARCHAR` on MSSQL,
    /// which stores UTF-16 and measures length in characters instead of bytes. On
    /// SQLite/PostgreSQL the Unicode variants fall back to TEXT affinity — same
    /// runtime behaviour as `VARCHAR`.
    bool forceUnicode = false;

    /// @brief Multiplier applied to every parameterised character-column size.
    ///
    /// LUP's `varchar(N)` was authored against codepage backends (Sybase / Oracle / MSSQL
    /// with cp1252 collation) where `N` is bytes and `byte == character`. PostgreSQL
    /// (and PG-style varchar in general) treats `N` as *characters*, but the actual
    /// LUP data already mixes German umlauts that legitimately occupy >1 byte once
    /// re-encoded as UTF-8 — and several legacy INSERTs are >100 chars long even
    /// before the multi-byte expansion. Scaling sizes here gives every backend
    /// enough headroom to accept that data without changing the source SQL.
    /// Default `1` keeps existing behaviour and tests; bump (typically `2` or `4`)
    /// when generating migrations for a Unicode-byte-counting dialect like PG.
    int varcharScale = 1;
};

/// @brief Generates C++ migration code from parsed migrations.
class CodeGenerator
{
  public:
    explicit CodeGenerator(CodeGeneratorConfig config = {});

    /// @brief Generates C++ code for a single migration.
    ///
    /// @param migration The parsed migration
    /// @param out Output stream to write to
    /// @param diagnostics Diagnostics collector for warnings/errors
    /// @return true if code was generated successfully (may still have warnings), false on fatal error
    bool GenerateMigration(ParsedMigration const& migration,
                           std::ostream& out,
                           std::vector<CodeGeneratorDiagnostic>& diagnostics) const;

    /// @brief Formats every statement in a migration to its own pre-rendered C++ block.
    ///
    /// Each returned string contains the leading comments plus the statement text, indented
    /// for inclusion inside a function body (i.e. 4-space indent). Use together with
    /// `GenerateMigrationHeaderComment` and the split-emission helpers in `main.cpp` when a
    /// migration is large enough to warrant being spread across multiple `.cpp` TUs.
    [[nodiscard]] std::vector<std::string> GenerateStatementBlocks(
        ParsedMigration const& migration,
        std::vector<CodeGeneratorDiagnostic>& diagnostics) const;

    /// @brief Writes the per-migration banner comment (separator + source-file + versions).
    /// Used by both the single-file and split-file emitters for consistent output.
    void WriteMigrationHeaderComment(ParsedMigration const& migration, std::ostream& out) const;

    /// @brief Emits a `LIGHTWEIGHT_SQL_RELEASE` marker for the given migration's version.
    ///
    /// Each parsed LUP migration file corresponds to exactly one release (the LUP
    /// version it targets). The emitted marker registers the release with the migration
    /// manager at static-init time so `dbtool status` and the migrations-gui can
    /// surface release progress ("Latest release: 6.8.8 (applied, 1/1 migrations)").
    /// Shared by the single-file and split-file emitters.
    void WriteReleaseMarker(ParsedMigration const& migration, std::ostream& out) const;

    /// @brief Generates C++ code for multiple migrations (single file mode).
    ///
    /// @param migrations The parsed migrations
    /// @param out Output stream to write to
    /// @param diagnostics Diagnostics collector for warnings/errors
    /// @return true if code was generated successfully (may still have warnings), false on fatal error
    bool GenerateAllMigrations(std::vector<ParsedMigration> const& migrations,
                               std::ostream& out,
                               std::vector<CodeGeneratorDiagnostic>& diagnostics) const;

    /// @brief Generates the file header with includes and namespace opening.
    void GenerateFileHeader(std::ostream& out) const;

    /// @brief Generates the file footer with namespace closing.
    void GenerateFileFooter(std::ostream& out) const;

    /// @brief Maps a SQL type string (e.g. "long varchar", "decimal(10,2)") to a Lightweight DSL
    /// type constructor expression (e.g. "Text()", "Decimal(10, 2)").
    ///
    /// Input is whitespace-normalized and matched case-insensitively. Multi-word types such as
    /// `long varchar` and `long varbinary` are supported. Parameterized types are recognized by
    /// the presence of parentheses with digits. Returns std::unexpected if the type is unknown.
    ///
    /// When `forceUnicode` is true, byte-char types are widened: `CHAR(n)` → `NChar(n)`,
    /// `VARCHAR(n)` → `NVarchar(n)`, `LONG VARCHAR` → `NText`-equivalent (still `Text()`
    /// in the Lightweight DSL — the dialect formatter handles the MSSQL `NTEXT` mapping).
    [[nodiscard]] static std::expected<std::string, std::monostate> MapSqlType(
        std::string_view sqlType, bool forceUnicode = false, int varcharScale = 1);

    /// @brief Writes a CMakeLists.txt for a self-contained migration plugin.
    ///
    /// The generated script globs every `lup_*.cpp` beside it and compiles them together
    /// with the emitted Plugin.cpp into a MODULE library named @p pluginName. The
    /// parent project can consume it with `add_subdirectory(<output-dir>)`.
    static void GeneratePluginCMake(std::ostream& out, std::string_view pluginName);

    /// @brief Writes a minimal Plugin.cpp that contains the LIGHTWEIGHT_MIGRATION_PLUGIN()
    /// entry-point macro and nothing else — migrations self-register via static init.
    static void GeneratePluginEntryPoint(std::ostream& out);

    /// @brief Escapes a string literal for direct embedding between C++ double quotes.
    /// Handles `\` and `"`; leaves other characters untouched.
    [[nodiscard]] static std::string EscapeForCppStringLiteral(std::string_view str);

  private:
    bool WriteStatementCode(ParsedStatement const& stmt,
                            std::ostream& out,
                            std::string const& indent,
                            std::vector<CodeGeneratorDiagnostic>& diagnostics) const;
    bool WriteCreateTable(CreateTableStmt const& stmt,
                          std::ostream& out,
                          std::string const& indent,
                          std::vector<CodeGeneratorDiagnostic>& diagnostics) const;
    bool WriteAlterTableAddColumn(AlterTableAddColumnStmt const& stmt,
                                  std::ostream& out,
                                  std::string const& indent,
                                  std::vector<CodeGeneratorDiagnostic>& diagnostics) const;
    static void WriteAlterTableAddForeignKey(AlterTableAddForeignKeyStmt const& stmt,
                                             std::ostream& out,
                                             std::string const& indent);
    static void WriteAlterTableAddCompositeForeignKey(AlterTableAddCompositeForeignKeyStmt const& stmt,
                                                      std::ostream& out,
                                                      std::string const& indent);
    static void WriteAlterTableDropForeignKey(AlterTableDropForeignKeyStmt const& stmt,
                                              std::ostream& out,
                                              std::string const& indent);
    static void WriteCreateIndex(CreateIndexStmt const& stmt, std::ostream& out, std::string const& indent);
    static void WriteDropTable(DropTableStmt const& stmt, std::ostream& out, std::string const& indent);
    static void WriteInsert(InsertStmt const& stmt, std::ostream& out, std::string const& indent);
    static void WriteUpdate(UpdateStmt const& stmt, std::ostream& out, std::string const& indent);
    static void WriteDelete(DeleteStmt const& stmt, std::ostream& out, std::string const& indent);
    static void WriteRawSql(RawSqlStmt const& stmt, std::ostream& out, std::string const& indent);

    [[nodiscard]] static std::string FormatValueLiteral(std::string_view value);

    CodeGeneratorConfig _config;
};

/// @brief Resolves output filename pattern with version substitution.
///
/// Supports patterns like:
/// - "output.cpp" -> "output.cpp" (no substitution)
/// - "lup_{major}_{minor}_{patch}.cpp" -> "lup_6_08_08.cpp"
/// - "lup_{version}.cpp" -> "lup_6_08_08.cpp"
///
/// @param pattern The output filename pattern
/// @param version The version to substitute
/// @return Resolved filename
[[nodiscard]] std::string ResolveOutputPattern(std::string_view pattern, LupVersion const& version);

/// @brief Checks if the output pattern requires multiple files.
[[nodiscard]] bool IsMultiFilePattern(std::string_view pattern);

} // namespace Lup2DbTool
