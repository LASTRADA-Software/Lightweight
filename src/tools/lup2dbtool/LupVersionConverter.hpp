// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Lup2DbTool
{

/// @brief Represents a LUP version number.
struct LupVersion
{
    int major { 0 };
    int minor { 0 };
    int patch { 0 };

    /// @brief Converts the version to an integer encoding.
    ///
    /// Version encoding:
    /// - 2.1.6 -> 216
    /// - 3.0.0 -> 300
    /// - 6.0.0 -> 60000
    /// - 6.8.8 -> 60808
    [[nodiscard]] int64_t ToInteger() const noexcept;

    /// @brief Converts the version to a migration timestamp.
    ///
    /// Migration timestamps use a fixed prefix (20000000) followed by the integer encoding:
    /// - 2.1.6 -> 20000000000216
    /// - 6.8.8 -> 20000000060808
    [[nodiscard]] uint64_t ToMigrationTimestamp() const noexcept;

    /// @brief Converts the version to a string in the format "X_Y_Z".
    [[nodiscard]] std::string ToString() const;

    /// @brief Parses a version string like "6.8.8" or "6_08_08".
    [[nodiscard]] static std::optional<LupVersion> Parse(std::string_view versionStr);

    auto operator<=>(LupVersion const&) const = default;
};

/// @brief Parses version information from a LUP migration filename.
///
/// Supports:
/// - init_m_MAJOR_MINOR_PATCH.sql (e.g., init_m_2_1_5.sql)
/// - upd_m_MAJOR_MINOR_PATCH.sql (e.g., upd_m_6_08_08.sql)
/// - upd_m_MAJOR_MINOR_PATCH__MAJOR_MINOR_PATCH.sql (e.g., upd_m_2_1_6__2_1_9.sql)
///
/// @return The target version (last version in the filename).
[[nodiscard]] std::optional<LupVersion> ParseFilename(std::string_view filename);

/// @brief Checks if the filename represents an initial migration (init_m_*.sql).
[[nodiscard]] bool IsInitMigration(std::string_view filename);

/// @brief Checks if the filename represents an update migration (upd_m_*.sql).
[[nodiscard]] bool IsUpdateMigration(std::string_view filename);

} // namespace Lup2DbTool
