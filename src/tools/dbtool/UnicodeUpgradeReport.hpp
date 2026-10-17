// SPDX-License-Identifier: Apache-2.0
//
// The plan `dbtool unicode-upgrade-tables` prints before it changes anything.

#pragma once

#include <Lightweight/SqlMigration.hpp>

#include <ostream>

namespace Lightweight::Tools
{

/// Writes the plan of `unicode-upgrade-tables` to @p out: the columns to upgrade, the units
/// committed one transaction each, and the objects to drop and recreate.
///
/// Tables are written as `<table>`, or as `<schema>.<table>` when the columns span several
/// schemas.
///
/// @param out  The stream to write to.
/// @param plan The result of a dry run of `MigrationManager::UnicodeUpgradeTables`.
void PrintUnicodeUpgradePlan(std::ostream& out, SqlMigration::MigrationManager::UnicodeUpgradeResult const& plan);

} // namespace Lightweight::Tools
