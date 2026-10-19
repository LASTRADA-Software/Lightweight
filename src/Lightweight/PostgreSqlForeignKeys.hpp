// SPDX-License-Identifier: Apache-2.0
//
// Internal to the Lightweight library (not installed): scripts the PostgreSQL foreign keys
// that touch columns about to change type, so `MigrationManager::UnicodeUpgradeTables` can
// drop them by their catalog names and recreate them exactly.

#pragma once

#include "SqlServerDependentObjects.hpp"

#include <vector>

namespace Lightweight
{

class SqlStatement;

namespace detail
{

    /// Reads the PostgreSQL catalog and scripts every foreign key with a column of `columns` on
    /// either side, under its own name and with its own definition (columns, referenced table
    /// with its schema, ON DELETE / ON UPDATE actions, deferrability).
    ///
    /// Reads the definitions with only `pg_catalog` on `search_path`, so that they name every
    /// referenced table with its schema, and restores the caller's path (transaction-local) before
    /// it returns. Partition clones of a key are left to the key on the partitioned table.
    ///
    /// @param stmt Statement on a PostgreSQL connection, inside the transaction of the change.
    /// @param columns Columns about to be retyped; each carries its schema.
    /// @return The drop / recreate script (`descriptions` stays empty, `needsAnsiWarnings` false).
    [[nodiscard]] DependentObjectScript ScriptPostgreSqlForeignKeys(SqlStatement& stmt,
                                                                    std::vector<ColumnToRetype> const& columns);

} // namespace detail

} // namespace Lightweight
