// SPDX-License-Identifier: Apache-2.0
//
// Decides which database a dbtool run connects to. Precedence, first match wins:
//
//   1. `--connection-string`;
//   2. `--profile` (from `--config` or a discovered dbtool.yml);
//   3. `--config` without `--profile`: that file's `defaultProfile`;
//   4. `SQL_CONNECTION_STRING`, then `ODBC_CONNECTION_STRING`;
//   5. the `defaultProfile` of a dbtool.yml found by discovery.
//
// Only the last source is implicit, so commands that write refuse it (see `AllowsWrites`).
// Kept free of I/O and environment access so it can be tested exhaustively.

#pragma once

#include "ConfigDiscovery.hpp"

#include <Lightweight/SqlConnectInfo.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Lightweight::Config
{

/// Where the connection target of a run came from.
enum class ConnectionTargetSource : std::uint8_t
{
    /// `--connection-string`.
    CommandLine,
    /// A profile named by `--profile`.
    ExplicitProfile,
    /// The `defaultProfile` of the file named by `--config`.
    ExplicitConfig,
    /// `SQL_CONNECTION_STRING` or `ODBC_CONNECTION_STRING`.
    Environment,
    /// The `defaultProfile` of a dbtool.yml found by discovery, not named by the user.
    DiscoveredConfig,
    /// Nothing supplies a connection.
    None,
};

/// Everything the target decision depends on.
struct ConnectionTargetInputs
{
    /// `--connection-string` was given.
    bool commandLineConnectionString = false;

    /// `--profile` was given.
    bool explicitProfile = false;

    /// Which rule found the config file; `ConfigSource::None` when there is none.
    ConfigSource configSource = ConfigSource::None;

    /// The selected profile (the named one, else the file's default) has a connection string or DSN.
    bool profileHasConnection = false;

    /// `SQL_CONNECTION_STRING` or `ODBC_CONNECTION_STRING` is set and not empty.
    bool environmentConnectionString = false;
};

/// The decision: the target's source, and whether the selected profile's other settings
/// (schema, plugins and backup folders) apply to the run.
struct ConnectionTarget
{
    /// Where the connection string comes from.
    ConnectionTargetSource source = ConnectionTargetSource::None;

    /// True when the selected profile's settings apply. A profile that another source overrides
    /// applies only when the user named it (`--profile` or `--config`), or when it has no
    /// connection of its own that could point at a different server.
    bool appliesProfileSettings = false;
};

/// Applies the precedence described at the top of this file.
/// @param inputs What the command line, the environment and the config file supply.
/// @return The chosen source and whether the profile's settings apply.
[[nodiscard]] ConnectionTarget ResolveConnectionTarget(ConnectionTargetInputs const& inputs) noexcept;

/// Whether the selected profile supplies the connection string.
/// @param source The resolved source.
/// @return True for `ExplicitProfile`, `ExplicitConfig` and `DiscoveredConfig`.
[[nodiscard]] bool IsProfileSource(ConnectionTargetSource source) noexcept;

/// Whether a command that writes may use a target from @p source.
/// @param source The resolved source.
/// @return False for `DiscoveredConfig` (implicit) and `None`; true for every explicit source.
[[nodiscard]] bool AllowsWrites(ConnectionTargetSource source) noexcept;

/// Explains, for a message, why a config file's first profile served as its default.
/// @param declaredDefault The file's `defaultProfile` as written; empty when it sets none.
/// @return E.g. " (the first profile; the file sets no defaultProfile)", with a leading space.
[[nodiscard]] std::string FirstProfileFallbackNote(std::string_view declaredDefault);

/// Describes a connection string by its server, database and DSN only, never its credentials.
/// @param connectionString The connection string.
/// @return E.g. "server 127.0.0.1,1444, database inventory", or "DSN prod"; when none of those
///         attributes is present, "a connection string without server, database or DSN".
[[nodiscard]] std::string DescribeConnectionTarget(SqlConnectionString const& connectionString);

} // namespace Lightweight::Config
