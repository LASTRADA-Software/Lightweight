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

/// @brief Default capacity of a connection's prepared-statement cache: the number of already-prepared
/// ODBC statement handles kept alive for reuse.
///
/// Zero — the cache is opt-in. Reusing a prepared handle also reuses the query plan the driver derived
/// from the schema at preparation time, so enabling it is a deliberate per-connection decision. See
/// @c SqlConnection::SetPreparedStatementCacheCapacity and
/// @ref SqlConnectionDataSource::preparedStatementCacheCapacity.
inline constexpr std::size_t PreparedStatementCacheCapacityDefault = 0;

/// @brief A sensible capacity for enabling the prepared-statement cache on a connection serving a
/// bounded set of recurring queries (the typical DataMapper workload).
inline constexpr std::size_t PreparedStatementCacheCapacitySuggested = 64;

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
/// @c true / @c false, @c 1 / @c 0, and the ODBC Driver 18 synonyms @c mandatory / @c optional.
///
/// @warning @c SqlEncryptionMode has no representation for ODBC Driver 18's @c strict (TDS 8.0 with
///          mandatory certificate validation), so @c Encrypt=strict parses as
///          @c SqlEncryptionMode::DriverDefault and is *dropped* by a subsequent
///          @ref SqlConnectionDataSource::ToConnectionString(). Keep such connection strings as a raw
///          @ref SqlConnectionString instead of round-tripping them through a data source.
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
///
/// Keys are upper-cased. Values follow the driver managers' quoting rules: a value that starts
/// with @c { runs to the matching @c } even across @c ; characters, and a doubled @c }} inside it
/// stands for one literal @c }. A value without a closing brace runs to the end of the string.
LIGHTWEIGHT_API SqlConnectionStringMap ParseConnectionString(SqlConnectionString const& connectionString);

/// Builds an ODBC connection string from a map.
///
/// Every value is rendered through @c FormatConnectionStringValue(), so the result parses back
/// into the same map whatever characters the values contain.
LIGHTWEIGHT_API SqlConnectionString BuildConnectionString(SqlConnectionStringMap const& map);

/// Renders @p value so that it can be spliced after @c KEY= in an ODBC connection string.
///
/// A value containing one of the connection-string metacharacters @c ; @c = @c { @c }, or starting
/// or ending with whitespace (which every parser trims away), is wrapped in braces with every
/// embedded @c } doubled — @c p;w becomes @c {p;w} and @c a}b becomes @c {a}}b}. That is the quoting
/// ODBC defines for attribute values: the driver managers and the SQL Server and PostgreSQL drivers
/// read it back, whereas the SQLite ODBC driver takes a braced @c Database= path literally. Any
/// other value is returned verbatim, so the common case keeps its plain spelling.
///
/// @param value The raw attribute value.
/// @return The spelling to splice after @c KEY=.
[[nodiscard]] LIGHTWEIGHT_API std::string FormatConnectionStringValue(std::string_view value);

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

    /// @brief Capacity of the prepared-statement cache on the resulting connection: how many
    /// already-prepared ODBC statement handles are kept alive so that re-preparing the same SQL text
    /// re-executes one instead of paying the server-side parse again.
    ///
    /// Defaults to @c PreparedStatementCacheCapacityDefault (zero, i.e. disabled). See
    /// @c SqlConnection::SetPreparedStatementCacheCapacity for the implications of enabling it.
    std::size_t preparedStatementCacheCapacity = PreparedStatementCacheCapacityDefault;

    /// Constructs a SqlConnectionDataSource from the given connection string.
    LIGHTWEIGHT_API static SqlConnectionDataSource FromConnectionString(SqlConnectionString const& value);

    /// Converts this data source to an ODBC connection string.
    ///
    /// The @c Encrypt= keyword is emitted only when @ref encryption is not
    /// @c SqlEncryptionMode::DriverDefault, so the rendering of a data source that did not opt in is
    /// byte-for-byte what it always was.
    ///
    /// @ref datasource, @ref username and @ref password go through @c FormatConnectionStringValue(),
    /// so a password such as @c p;w reaches the driver intact while plain credentials keep their
    /// unquoted spelling.
    [[nodiscard]] LIGHTWEIGHT_API SqlConnectionString ToConnectionString() const;

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
