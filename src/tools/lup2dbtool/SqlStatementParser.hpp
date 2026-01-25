// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Lup2DbTool
{

/// @brief Represents a column definition in a CREATE TABLE statement.
struct ColumnDef
{
    std::string name;
    std::string type;       // e.g., "integer", "varchar(50)"
    bool isNullable = true; // true = NULL allowed (default), false = NOT NULL
    bool isPrimaryKey = false;
};

/// @brief Represents a foreign key definition.
struct ForeignKeyDef
{
    std::string columnName;
    std::string referencedTable;
    std::string referencedColumn;
};

/// @brief Represents a CREATE TABLE statement.
struct CreateTableStmt
{
    std::string tableName;
    std::vector<ColumnDef> columns;
    std::vector<ForeignKeyDef> foreignKeys;
};

/// @brief Represents an ALTER TABLE ADD COLUMN statement.
struct AlterTableAddColumnStmt
{
    std::string tableName;
    ColumnDef column;
};

/// @brief Represents an ALTER TABLE ADD FOREIGN KEY statement.
struct AlterTableAddForeignKeyStmt
{
    std::string tableName;
    ForeignKeyDef foreignKey;
};

/// @brief Represents an ALTER TABLE ADD with composite foreign key (multiple columns).
struct AlterTableAddCompositeForeignKeyStmt
{
    std::string tableName;
    std::vector<std::string> columns;
    std::string referencedTable;
    std::vector<std::string> referencedColumns;
};

/// @brief Represents an ALTER TABLE DROP FOREIGN KEY statement.
struct AlterTableDropForeignKeyStmt
{
    std::string tableName;
    std::string columnName;
    std::string referencedTable;
    std::string referencedColumn;
};

/// @brief Represents an INSERT statement.
struct InsertStmt
{
    std::string tableName;
    std::vector<std::pair<std::string, std::string>> columnValues; // column name -> value literal
};

/// @brief Represents an UPDATE statement.
struct UpdateStmt
{
    std::string tableName;
    std::vector<std::pair<std::string, std::string>> setColumns; // column name -> value literal
    std::string whereColumn;
    std::string whereOp;
    std::string whereValue;
};

/// @brief Represents a DELETE statement.
struct DeleteStmt
{
    std::string tableName;
    std::string whereColumn;
    std::string whereOp;
    std::string whereValue;
};

/// @brief Represents a CREATE INDEX statement.
struct CreateIndexStmt
{
    std::string indexName;
    std::string tableName;
    std::vector<std::string> columns;
    bool unique = false;
};

/// @brief Represents a DROP TABLE statement.
struct DropTableStmt
{
    std::string tableName;
};

/// @brief Represents a raw SQL statement that couldn't be parsed.
struct RawSqlStmt
{
    std::string sql;
};

/// @brief Represents a parsed SQL statement.
using ParsedStatement = std::variant<CreateTableStmt,
                                     AlterTableAddColumnStmt,
                                     AlterTableAddForeignKeyStmt,
                                     AlterTableAddCompositeForeignKeyStmt,
                                     AlterTableDropForeignKeyStmt,
                                     CreateIndexStmt,
                                     DropTableStmt,
                                     InsertStmt,
                                     UpdateStmt,
                                     DeleteStmt,
                                     RawSqlStmt>;

/// @brief Parses a SQL statement into a structured AST.
///
/// This parser handles a subset of SQL syntax used in LUP migration files:
/// - CREATE TABLE with column definitions and constraints
/// - ALTER TABLE ADD COLUMN
/// - ALTER TABLE ADD FOREIGN KEY
/// - INSERT INTO ... VALUES
/// - UPDATE ... SET ... WHERE
/// - DELETE FROM ... WHERE
///
/// If the statement cannot be parsed, it returns a RawSqlStmt.
[[nodiscard]] ParsedStatement ParseSqlStatement(std::string_view sql);

/// @brief Checks if the statement is a CREATE TABLE statement.
[[nodiscard]] bool IsCreateTable(std::string_view sql);

/// @brief Checks if the statement is an ALTER TABLE statement.
[[nodiscard]] bool IsAlterTable(std::string_view sql);

/// @brief Checks if the statement is an INSERT statement.
[[nodiscard]] bool IsInsert(std::string_view sql);

/// @brief Checks if the statement is an UPDATE statement.
[[nodiscard]] bool IsUpdate(std::string_view sql);

/// @brief Checks if the statement is a DELETE statement.
[[nodiscard]] bool IsDelete(std::string_view sql);

/// @brief Checks if the statement is a CREATE INDEX statement.
[[nodiscard]] bool IsCreateIndex(std::string_view sql);

/// @brief Checks if the statement is a DROP TABLE statement.
[[nodiscard]] bool IsDropTable(std::string_view sql);

} // namespace Lup2DbTool
