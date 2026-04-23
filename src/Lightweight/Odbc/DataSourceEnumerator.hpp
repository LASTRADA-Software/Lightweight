// SPDX-License-Identifier: Apache-2.0
//
// Thin wrapper over the ODBC driver manager's `SQLDataSources` / `SQLDrivers`
// entry points. Used by the migrations GUI (to populate the DSN picker) and
// exposed as a library-level utility so other Lightweight-based apps can reuse
// it without copy-pasting the handle lifetime plumbing.
//
// The enumerator does not require an open `SqlConnection`: it operates on its
// own scratch `SQL_HANDLE_ENV`. Queries are synchronous and can be slow on
// Windows when there are many installed drivers — call from a worker thread
// in GUI contexts.

#pragma once

#include "../Api.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Lightweight::Odbc
{

/// A single ODBC data source as reported by `SQLDataSources`.
struct DataSourceInfo
{
    /// DSN name, suitable for use in `DRIVER=...;DSN=<name>;…`.
    std::string name;

    /// Driver description string (the driver's friendly name, e.g. "SQL Server"
    /// or "ODBC Driver 18 for SQL Server"). May be empty if the driver manager
    /// declined to report one.
    std::string description;

    /// Scope of the data source registration — user-private or system-wide.
    enum class Scope : std::uint8_t
    {
        User,
        System,
    };
    Scope scope = Scope::User;

    auto operator<=>(DataSourceInfo const&) const noexcept = default;
};

/// A single installed ODBC driver as reported by `SQLDrivers`.
struct DriverInfo
{
    /// Driver name, e.g. "ODBC Driver 18 for SQL Server".
    std::string name;

    /// Driver attribute key/value pairs as returned by the driver manager.
    /// Typical keys include "FileUsage", "APILevel", "ConnectFunctions".
    std::vector<std::pair<std::string, std::string>> attributes;

    auto operator<=>(DriverInfo const&) const noexcept = default;
};

/// Enumerates every ODBC data source visible to the driver manager.
///
/// On success the returned vector contains user DSNs first, then system DSNs,
/// in the order the driver manager reports them. On failure (driver manager
/// not installed, environment handle allocation failed) the vector is empty.
///
/// The call is synchronous. Call from a worker thread if you need to keep a
/// GUI event loop responsive.
[[nodiscard]] LIGHTWEIGHT_API std::vector<DataSourceInfo> EnumerateDataSources();

/// Enumerates every ODBC driver installed on the system.
///
/// Same semantics as `EnumerateDataSources` — synchronous, empty vector on
/// failure.
[[nodiscard]] LIGHTWEIGHT_API std::vector<DriverInfo> EnumerateDrivers();

} // namespace Lightweight::Odbc
