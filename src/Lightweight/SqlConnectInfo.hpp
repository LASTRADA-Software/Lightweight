// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <string_view>
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

/// @ingroup CoreApi
/// @brief Whether the client/server connection is TLS-encrypted.
///
/// Maps onto the Microsoft SQL Server ODBC connection attribute @c SQL_COPT_SS_ENCRYPT, which must be
/// set on the connection handle *before* connecting. This is the only way to request encryption on the
/// DSN-based connect path (@c SQLConnect), where there is no connection string for an @c Encrypt=
/// keyword to live in.
///
/// @see https://learn.microsoft.com/en-us/sql/relational-databases/native-client-odbc-api/sqlsetconnectattr
enum class SqlEncryptionMode : std::uint8_t
{
    /// Leave the attribute untouched — whatever the driver, DSN, or connection string configures wins.
    ///
    /// This is the default, so an application that does not opt in behaves exactly as before.
    DriverDefault = 0,

    /// Request an unencrypted connection (@c SQL_EN_OFF).
    Disabled = 1,

    /// Request an encrypted connection (@c SQL_EN_ON).
    Enabled = 2,
};

/// Parses an ODBC @c Encrypt= connection-string value into a @ref SqlEncryptionMode.
///
/// Recognizes the spellings the SQL Server drivers accept, case-insensitively: @c yes / @c no,
/// @c true / @c false, and @c 1 / @c 0.
///
/// @param value The raw keyword value.
/// @return The matching mode, or @c SqlEncryptionMode::DriverDefault if @p value is not recognized.
[[nodiscard]] LIGHTWEIGHT_API SqlEncryptionMode ParseEncryptionMode(std::string_view value) noexcept;

/// Renders a @ref SqlEncryptionMode as the ODBC @c Encrypt= connection-string value.
///
/// @param mode The mode to render.
/// @return @c "yes" or @c "no", or an empty view for @c SqlEncryptionMode::DriverDefault (which is
///         expressed by omitting the keyword entirely).
[[nodiscard]] LIGHTWEIGHT_API std::string_view FormatEncryptionMode(SqlEncryptionMode mode) noexcept;

/// @ingroup CoreApi
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

/// @ingroup CoreApi
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
    /// Defaults to @c PrefetchDepthDefault. Has effect only on backends whose driver supports
    /// native row-array fetching (see @c SqlConnection::SupportsNativeRowArrayFetch).
    std::size_t defaultPrefetchDepth = PrefetchDepthDefault;

    /// @brief Whether to request a TLS-encrypted connection.
    ///
    /// Defaults to @c SqlEncryptionMode::DriverDefault, which leaves the driver's own configuration in
    /// charge. Any other value is applied to the connection handle before connecting, and a driver that
    /// rejects it fails the connection rather than silently downgrading to plaintext.
    SqlEncryptionMode encryption = SqlEncryptionMode::DriverDefault;

    /// Constructs a SqlConnectionDataSource from the given connection string.
    LIGHTWEIGHT_API static SqlConnectionDataSource FromConnectionString(SqlConnectionString const& value);

    /// Converts this data source to an ODBC connection string.
    ///
    /// The @c Encrypt= keyword is emitted only when @ref encryption is not
    /// @c SqlEncryptionMode::DriverDefault, so the rendering of a data source that did not opt in is
    /// byte-for-byte what it always was.
    [[nodiscard]] LIGHTWEIGHT_API SqlConnectionString ToConnectionString() const
    {
        auto value = std::format("DSN={};UID={};PWD={};TIMEOUT={}", datasource, username, password, timeout.count());
        if (auto const encryptValue = FormatEncryptionMode(encryption); !encryptValue.empty())
            value += std::format(";Encrypt={}", encryptValue);
        return SqlConnectionString { .value = std::move(value) };
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
            return formatter<string>::format(dsn->ToConnectionString().value, ctx);
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
