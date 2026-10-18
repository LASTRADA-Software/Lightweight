// SPDX-License-Identifier: Apache-2.0
//
// Helpers the GUI's backup requirement code shares between the runners and the controller.

#pragma once

#include <Lightweight/SqlMigration.hpp>

#include <ranges>
#include <string_view>
#include <vector>

namespace DbtoolGui
{

/// The tool name the GUI's `schema_backups` records carry (see `Tools::ToolVersion`).
inline constexpr std::string_view RecordToolName = "dbtool-gui";

/// The timestamps of @p migrations, in their order.
/// @param migrations A range of `MigrationBase const*`.
/// @return One timestamp per migration.
template <std::ranges::input_range Migrations>
[[nodiscard]] std::vector<Lightweight::SqlMigration::MigrationTimestamp> TimestampsOf(Migrations const& migrations)
{
    auto timestamps = std::vector<Lightweight::SqlMigration::MigrationTimestamp> {};
    for (auto const* migration: migrations)
        timestamps.push_back(migration->GetTimestamp());
    return timestamps;
}

} // namespace DbtoolGui
