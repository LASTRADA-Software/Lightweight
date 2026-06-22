// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

#include <chrono>
#include <cstddef>
#include <format>
#include <map>
#include <string>
#include <variant>

namespace Lightweight
{

/// @brief Default block-prefetch depth for new connections: the number of rows a classic per-row
/// fetch loop requests per @c SQLFetchScroll round-trip on the transparent prefetch path.
///
/// Suffixed (not @c DefaultPrefetchDepth) so it does not collide with the
/// @c SqlConnection::DefaultPrefetchDepth() accessor. A connection's depth can be overridden via
/// @c SqlConnection::SetDefaultPrefetchDepth or @ref SqlConnectionDataSource::defaultPrefetchDepth;
/// a value <= 1 disables prefetch.
constexpr std::size_t PrefetchDepthDefault = 1000;

/// Represents an ODBC connection string.
struct SqlConnectionString
{
    /// The raw ODBC connection string value.
    std::string value;

    /// Three-way comparison operator.
    auto operator<=>(SqlConnectionString const&) const noexcept = default;

    /// Returns a sanitized copy of the connection string with the password masked.
    [[nodiscard]] LIGHTWEIGHT_API std::string Sanitized() const;

    /// Sanitizes the password in the given connection string input.
    [[nodiscard]] LIGHTWEIGHT_API static std::string SanitizePwd(std::string_view input);
};

using SqlConnectionStringMap = std::map<std::string, std::string>;

/// Parses an ODBC connection string into a map.
LIGHTWEIGHT_API SqlConnectionStringMap ParseConnectionString(SqlConnectionString const& connectionString);

/// Builds an ODBC connection string from a map.
LIGHTWEIGHT_API SqlConnectionString BuildConnectionString(SqlConnectionStringMap const& map);

/// If `connectionString` targets a file-based SQLite database, ensures the
/// parent directory exists and touches an empty file when missing.
///
/// An empty file is a valid zero-table SQLite database, so this lets callers
/// bootstrap a fresh SQLite deployment from scratch without requiring the
/// user to pre-create the file. In-memory databases (`:memory:`,
/// `file::memory:`, URIs with `mode=memory`) and non-SQLite drivers are
/// left untouched.
///
/// Returns true on success or when no action was needed. Returns false only
/// when the parent directory could not be created or the file could not be
/// opened for writing.
[[nodiscard]] LIGHTWEIGHT_API bool EnsureSqliteDatabaseFileExists(SqlConnectionString const& connectionString);

/// Represents a connection data source as a DSN, username, password, and timeout.
struct [[nodiscard]] SqlConnectionDataSource
{
    /// The ODBC data source name (DSN).
    std::string datasource;
    /// The username for authentication.
    std::string username;
    /// The password for authentication.
    std::string password;
    /// The connection timeout duration.
    std::chrono::seconds timeout { 5 };
    /// @brief Default block-prefetch depth applied to statements created on the resulting connection
    /// (rows requested per @c SQLFetchScroll round-trip on the transparent per-row fetch path).
    ///
    /// A value <= 1 disables prefetch (every classic loop keeps issuing one @c SQLFetch per row).
    /// Defaults to @ref PrefetchDepthDefault. Has effect only on backends whose driver supports
    /// native row-array fetching (see @c SqlConnection::SupportsNativeRowArrayFetch).
    std::size_t defaultPrefetchDepth = PrefetchDepthDefault;

    /// Constructs a SqlConnectionDataSource from the given connection string.
    LIGHTWEIGHT_API static SqlConnectionDataSource FromConnectionString(SqlConnectionString const& value);

    /// Converts this data source to an ODBC connection string.
    [[nodiscard]] LIGHTWEIGHT_API SqlConnectionString ToConnectionString() const
    {
        return SqlConnectionString {
            .value = std::format("DSN={};UID={};PWD={};TIMEOUT={}", datasource, username, password, timeout.count())
        };
    }

    /// Three-way comparison operator.
    auto operator<=>(SqlConnectionDataSource const&) const noexcept = default;
};

using SqlConnectInfo = std::variant<SqlConnectionDataSource, SqlConnectionString>;

} // namespace Lightweight

template <>
struct std::formatter<Lightweight::SqlConnectInfo>: std::formatter<std::string>
{
    auto format(Lightweight::SqlConnectInfo const& info, format_context& ctx) const -> format_context::iterator
    {
        if (auto const* dsn = std::get_if<Lightweight::SqlConnectionDataSource>(&info))
        {
            return formatter<string>::format(
                std::format(
                    "DSN={};UID={};PWD={};TIMEOUT={}", dsn->datasource, dsn->username, dsn->password, dsn->timeout.count()),
                ctx);
        }
        else if (auto const* connectionString = std::get_if<Lightweight::SqlConnectionString>(&info))
        {
            return formatter<string>::format(connectionString->value, ctx);
        }
        else
        {
            return formatter<string>::format("Invalid connection info", ctx);
        }
    }
};
