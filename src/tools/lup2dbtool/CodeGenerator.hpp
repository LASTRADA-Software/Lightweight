// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "LupSqlParser.hpp"

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace Lup2DbTool
{

/// @brief Configuration for C++ code generation.
struct CodeGeneratorConfig
{
    /// Namespace for generated migrations (default: "Lup::Migrations")
    std::string namespacePrefix = "Lup::Migrations";

    /// Include guard prefix (default: "LUP_MIGRATIONS_")
    std::string includeGuardPrefix = "LUP_MIGRATIONS_";
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
    void GenerateMigration(ParsedMigration const& migration, std::ostream& out) const;

    /// @brief Generates C++ code for multiple migrations (single file mode).
    ///
    /// @param migrations The parsed migrations
    /// @param out Output stream to write to
    void GenerateAllMigrations(std::vector<ParsedMigration> const& migrations, std::ostream& out) const;

    /// @brief Generates the file header with includes and namespace opening.
    void WriteFileHeader(std::ostream& out) const;

    /// @brief Generates the file footer with namespace closing.
    void WriteFileFooter(std::ostream& out) const;

  private:
    static void WriteStatementCode(ParsedStatement const& stmt, std::ostream& out, std::string const& indent);
    static void WriteCreateTable(CreateTableStmt const& stmt, std::ostream& out, std::string const& indent);
    static void WriteAlterTableAddColumn(AlterTableAddColumnStmt const& stmt, std::ostream& out, std::string const& indent);
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

    [[nodiscard]] static std::string MapSqlType(std::string_view sqlType);
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
