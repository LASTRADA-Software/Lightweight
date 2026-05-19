// SPDX-License-Identifier: Apache-2.0
//
// Shared plugin discover-load-merge orchestration for `dbtool` and
// `dbtool-gui`. Built on top of `Lightweight::Tools::DiscoverPlugins`
// (filename-based dedup) and parameterised on the per-path "load this
// plugin" step so unit tests can substitute a fake for the real
// `PluginLoader`.

#pragma once

#include "PluginDiscovery.hpp"
#include "PluginLoader.hpp"

#include <Lightweight/SqlMigration.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Lightweight::Tools
{

/// One successfully loaded plugin. `IngestPlugins` returns these so the
/// caller can keep the underlying shared library alive for the lifetime
/// of `central` — destroying `handle` is what triggers `dlclose` /
/// `FreeLibrary`.
struct LoadedPlugin
{
    /// Filesystem path the loader was invoked with.
    std::filesystem::path path;

    /// Type-erased owner of whatever the loader callable produced
    /// (`PluginLoader` in production, a test stub in unit tests). Never
    /// inspected by the orchestration — its only job is RAII.
    std::shared_ptr<void> handle;

    /// Pointer to the plugin's local `MigrationManager` (each plugin
    /// shared library has its own static instance, so two plugins yield
    /// two distinct pointers). Borrowed; lifetime is bound to `handle`.
    Lightweight::SqlMigration::MigrationManager* pluginManager { nullptr };

    /// Generic symbol lookup. Production: delegates to
    /// `PluginLoader::TryGetSymbol`. Tests: a stub that returns the
    /// function pointer for the symbol they want to verify a caller looks
    /// up. Empty `std::function` is treated as "no symbol available" by
    /// `TryGetFunction`. Used by post-load hooks (e.g. dbtool's
    /// `LightweightMigrationPluginPostInit`) that need to discover
    /// optional extension points without re-opening the shared library.
    std::function<PluginLoader::GenericFunctionPointer(std::string const&)> tryGetSymbol {};

    /// Type-safe wrapper around `tryGetSymbol`. Returns `nullptr` when
    /// the lookup is unavailable or the symbol is absent.
    template <typename FunctionSignature>
    [[nodiscard]] FunctionSignature* TryGetFunction(std::string const& symbolName) const
    {
        if (!tryGetSymbol)
            return nullptr;
        return reinterpret_cast<FunctionSignature*>(tryGetSymbol(symbolName));
    }
};

/// Per-path load callback. Result semantics:
///   - `std::nullopt`      → the path is not a plugin we want to keep
///                           (e.g. a shared library that does not export
///                           `AcquireMigrationManager`). Skipped silently
///                           so a multi-directory `defaultPluginsDir`
///                           can sit next to unrelated libraries.
///   - populated value     → a real plugin; its `pluginManager` will be
///                           merged into the host's `central` manager.
/// A throw signals an unrecoverable load failure; `IngestPlugins` funnels
/// it through `logError` and then re-throws iff `throwOnLoadError` is set.
using PluginLoaderFn = std::function<std::optional<LoadedPlugin>(std::filesystem::path const&)>;

/// Optional callbacks + behaviour flags. All callbacks are no-ops when
/// left empty so callers can opt in to whichever channels matter to them.
struct PluginIngestOptions
{
    /// Forwarded to `DiscoverPlugins` for "filename X from path A
    /// shadowed by path B (newer mtime)" notices.
    std::function<void(std::string_view)> logShadowed {};

    /// Fires once per plugin that was successfully loaded into `central`.
    /// dbtool routes this to `std::println` (matching its historic
    /// `"Loading plugin: <path>"` line).
    std::function<void(std::string_view)> logLoading {};

    /// Fires when the loader throws or when the duplicate-handle guard
    /// triggers. The message is a single line including the path and the
    /// underlying error text.
    std::function<void(std::string_view)> logError {};

    /// `true` (the default) reproduces dbtool's fail-fast behaviour: the
    /// first load exception is logged and then re-thrown. `false` is the
    /// GUI-friendly path — bad plugins are logged and skipped, the rest
    /// continue loading.
    bool throwOnLoadError { true };
};

/// Discover + load + merge. Single shared orchestration consumed by both
/// `dbtool` and `dbtool-gui`. Verifies the post-discovery shortlist with
/// `loader` (the DI seam), guards against duplicate native handles, and
/// copies every loaded plugin's releases, migrations, and compat policy
/// into `central`. Returns the surviving load handles in the order they
/// were resolved by `DiscoverPlugins`.
///
/// @param dirs     Directories handed to `DiscoverPlugins`.
/// @param loader   Per-path callback. In production this is the lambda
///                 returned by `DefaultPluginLoader()`; in tests it's a
///                 stub that maps paths to fake `MigrationManager`
///                 instances.
/// @param central  Host-side manager that aggregates state from every
///                 loaded plugin.
/// @param options  Logging callbacks + fail-fast flag.
[[nodiscard]] std::vector<LoadedPlugin> IngestPlugins(std::span<std::filesystem::path const> dirs,
                                                      PluginLoaderFn const& loader,
                                                      Lightweight::SqlMigration::MigrationManager& central,
                                                      PluginIngestOptions const& options = {});

/// Production loader: opens `path` with `Lightweight::Tools::PluginLoader`,
/// looks up the plugin's `AcquireMigrationManager` symbol, and returns the
/// resulting `LoadedPlugin`. Returns `std::nullopt` when the symbol is
/// absent (i.e. the library is not a migration plugin). Throws
/// `std::runtime_error` when `PluginLoader` construction itself fails.
[[nodiscard]] PluginLoaderFn DefaultPluginLoader();

} // namespace Lightweight::Tools
