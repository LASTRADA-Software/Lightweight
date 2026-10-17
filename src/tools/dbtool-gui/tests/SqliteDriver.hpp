// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace DbtoolGui::Tests
{

/// SQLite ODBC driver name as registered on this platform.
inline constexpr char const* SqliteDriver =
#ifdef _WIN32
    "SQLite3 ODBC Driver";
#else
    "SQLite3";
#endif

/// ODBC connection string for the SQLite database file `database`.
[[nodiscard]] inline std::string SqliteConnectionString(std::string const& database)
{
    return std::string { "DRIVER={" } + SqliteDriver + "};Database=" + database;
}

} // namespace DbtoolGui::Tests
