// SPDX-License-Identifier: Apache-2.0
//
// Internal to the Lightweight library (not installed): scripts the SQL Server objects
// that prevent changing a column's type, so `MigrationManager::UnicodeUpgradeTables`
// can drop them, alter the columns and recreate them exactly.
//
// SQL Server refuses `ALTER COLUMN` from CHAR/VARCHAR to NCHAR/NVARCHAR while the column
// is used by a primary-key, unique, foreign-key, default or check constraint, an index,
// or user-created statistics. The catalog (`sys.*`) is read once up front; every object
// is recreated under its original name with its original definition.

#pragma once

#include <string>
#include <vector>

namespace Lightweight
{

class SqlStatement;

namespace detail
{

    /// One column whose type is about to change.
    struct ColumnToRetype
    {
        /// Schema of the table; empty means the connection's default schema.
        std::string schema;
        /// Table name.
        std::string table;
        /// Column name.
        std::string column;
    };

    /// The SQL needed around an `ALTER COLUMN` batch: statements to run before it (in
    /// order), statements to run after it (in order), and a human-readable line per object.
    struct DependentObjectScript
    {
        /// Drops every dependent object, foreign keys first.
        std::vector<std::string> drop;
        /// Recreates every dependent object, referenced keys before the foreign keys.
        std::vector<std::string> recreate;
        /// One description per object, e.g. "PRIMARY KEY PK_users on dbo.users".
        std::vector<std::string> descriptions;
        /// True when a recreated index has a filter, which SQL Server only accepts with
        /// `ANSI_WARNINGS ON`.
        bool needsAnsiWarnings = false;
    };

    /// Reads the SQL Server catalog and scripts the objects that depend on `columns`.
    /// Objects that cannot be scripted safely (schema-bound views or functions, computed
    /// columns, non-rowstore indexes) make it throw `std::runtime_error` naming them,
    /// before anything has been changed.
    /// @param stmt Statement on a SQL Server connection.
    /// @param columns Columns about to be retyped.
    /// @return The drop / recreate script.
    [[nodiscard]] DependentObjectScript ScriptSqlServerDependentObjects(SqlStatement& stmt,
                                                                        std::vector<ColumnToRetype> const& columns);

} // namespace detail

} // namespace Lightweight
