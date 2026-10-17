// SPDX-License-Identifier: Apache-2.0

#include "PluginIngestion.hpp"
#include "PluginLoader.hpp"

#include <format>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace Lightweight::Tools
{

namespace
{

    /// Merges every release, migration, and compat policy installed on
    /// `plugin` into `central`. No-op when `plugin == &central` (a
    /// plugin loaded into the same process that exports its own
    /// `AcquireMigrationManager` returning the host's singleton would
    /// otherwise double-register everything).
    void MergeIntoCentral(Lightweight::SqlMigration::MigrationManager& plugin,
                          Lightweight::SqlMigration::MigrationManager& central)
    {
        if (&plugin == &central)
            return;

        // Releases first: a cross-plugin duplicate-release conflict
        // throws here, and we'd rather fail before any migrations have
        // been imported than land in a half-merged state.
        for (auto const& release: plugin.GetAllReleases())
            central.RegisterRelease(release.version, release.highestTimestamp);

        for (auto const* migration: plugin.GetAllMigrations())
            central.AddMigration(migration);

        // Compose (don't replace) so multiple plugins can each install
        // compat knobs in the same process.
        if (auto const& policy = plugin.GetCompatPolicy())
            central.ComposeCompatPolicy(policy);
    }

    /// `try_emplace`-style helper that fires `logError` and (depending on
    /// `throwOnLoadError`) throws when the same native handle is seen
    /// twice. Returns `true` when `incoming` is a duplicate (caller skips
    /// it). Two distinct shared libraries normally produce two distinct
    /// handles; this guard is a defence-in-depth check that catches
    /// symlink/junction collisions the filename heuristic cannot see.
    [[nodiscard]] bool RejectDuplicateHandle(std::unordered_set<void const*>& seen,
                                             LoadedPlugin const& incoming,
                                             PluginIngestOptions const& options)
    {
        if (seen.insert(incoming.handle.get()).second)
            return false;

        auto const msg = std::format("Failed to load plugin {}: duplicate handle", incoming.path.string());
        if (options.logError)
            options.logError(msg);
        if (options.throwOnLoadError)
            throw std::runtime_error(msg);
        return true;
    }

    /// Wraps `loader(path)` so a thrown exception is funnelled through
    /// `logError` exactly once and (depending on `throwOnLoadError`)
    /// re-thrown to the orchestration's caller. Returns `nullopt` for
    /// "skip this path" — either because the loader said so or because
    /// the loader threw but the caller asked to continue.
    [[nodiscard]] std::optional<LoadedPlugin> InvokeLoader(PluginLoaderFn const& loader,
                                                           std::filesystem::path const& path,
                                                           PluginIngestOptions const& options)
    {
        try
        {
            return loader(path);
        }
        catch (std::exception const& e)
        {
            auto const msg = std::format("Failed to load plugin {}: {}", path.string(), e.what());
            if (options.logError)
                options.logError(msg);
            if (options.throwOnLoadError)
                throw;
            return std::nullopt;
        }
    }

} // namespace

std::vector<LoadedPlugin> IngestPlugins(std::span<std::filesystem::path const> dirs,
                                        PluginLoaderFn const& loader,
                                        Lightweight::SqlMigration::MigrationManager& central,
                                        PluginIngestOptions const& options)
{
    auto const resolved = DiscoverPlugins(dirs, options.logShadowed);

    std::vector<LoadedPlugin> loaded;
    loaded.reserve(resolved.size());

    std::unordered_set<void const*> seenHandles;
    try
    {
        for (auto const& entry: resolved)
        {
            auto maybe = InvokeLoader(loader, entry.path, options);
            if (!maybe)
                continue; // either nullopt-from-loader, or threw and we're not fail-fast

            if (RejectDuplicateHandle(seenHandles, *maybe, options))
                continue;

            if (options.logLoading)
                options.logLoading(std::format("Loading plugin: {}", maybe->path.string()));

            // Owned by `loaded` before merging, so a failed merge still unloads it through `UnloadPlugins`.
            auto const& plugin = loaded.emplace_back(std::move(*maybe));
            if (plugin.pluginManager != nullptr)
                MergeIntoCentral(*plugin.pluginManager, central);
        }
    }
    catch (...)
    {
        UnloadPlugins(loaded, central);
        throw;
    }

    return loaded;
}

void UnloadPlugins(std::vector<LoadedPlugin>& plugins, Lightweight::SqlMigration::MigrationManager& central)
{
    central.SetCompatPolicy({});
    central.RemoveAllMigrations();
    central.RemoveAllReleases();
    central.CloseDataMapper();
    plugins.clear();
}

PluginLoaderFn DefaultPluginLoader()
{
    return [](std::filesystem::path const& path) -> std::optional<LoadedPlugin> {
        auto loader = std::make_shared<PluginLoader>(path);

        // `TryGetFunction` returns nullptr for "symbol absent" — that's
        // the silent-skip path (an unrelated `.so`/`.dll` sitting in a
        // multi-directory `defaultPluginsDir`). Anything else (e.g.
        // `dlsym` error other than not-found) surfaces as an exception
        // and reaches `IngestPlugins::InvokeLoader`.
        auto* acquire = loader->TryGetFunction<Lightweight::SqlMigration::MigrationManager*()>("AcquireMigrationManager");
        if (acquire == nullptr)
            return std::nullopt;

        // Capture by value (not by raw pointer) so the symbol-lookup
        // closure keeps the underlying `PluginLoader` alive even if the
        // caller resets the `LoadedPlugin::handle` field separately —
        // the two ownership channels stay independent.
        return LoadedPlugin {
            .path = path,
            .handle = std::static_pointer_cast<void>(loader),
            .pluginManager = acquire(),
            .tryGetSymbol = [loader](std::string const& name) { return loader->TryGetSymbol(name); },
        };
    };
}

void RunPluginPostInitHooks(std::span<LoadedPlugin const> plugins,
                            Lightweight::SqlConnection& connection,
                            Lightweight::SqlMigration::MigrationManager& central,
                            std::function<void(std::string_view)> const& logError)
{
    using HookSignature = void(Lightweight::SqlConnection&, Lightweight::SqlMigration::MigrationManager&);

    for (auto const& plugin: plugins)
    {
        auto* const postInit = plugin.TryGetFunction<HookSignature>(std::string { PostInitHookSymbol });
        if (postInit == nullptr)
            continue; // Optional symbol — plugins without legacy glue skip the hook.

        try
        {
            postInit(connection, central);
        }
        catch (std::exception const& exception)
        {
            if (logError)
                logError(std::format("Plugin post-init failed ({}): {}", plugin.path.string(), exception.what()));
        }
    }
}

std::vector<std::filesystem::path> PluginsWithPostInitHook(std::span<LoadedPlugin const> plugins)
{
    using HookSignature = void(Lightweight::SqlConnection&, Lightweight::SqlMigration::MigrationManager&);
    auto withHook = std::vector<std::filesystem::path> {};
    for (auto const& plugin: plugins)
        if (plugin.TryGetFunction<HookSignature>(std::string { PostInitHookSymbol }) != nullptr)
            withHook.push_back(plugin.path);
    return withHook;
}

bool RunPluginPostMigrateHooks(std::span<LoadedPlugin const> plugins,
                               Lightweight::SqlConnection& connection,
                               Lightweight::SqlMigration::MigrationManager& central,
                               std::function<void(std::string_view)> const& logError)
{
    using HookSignature = bool(Lightweight::SqlConnection&,
                               Lightweight::SqlMigration::MigrationManager&,
                               Lightweight::SqlMigration::PostMigrateErrorSink,
                               void*) noexcept;
    auto const collectMessage = [](void* context, char const* message) noexcept {
        *static_cast<std::string*>(context) = message != nullptr ? message : "";
    };

    auto allSucceeded = true;
    for (auto const& plugin: plugins)
    {
        auto* const postMigrate = plugin.TryGetFunction<HookSignature>("LightweightMigrationPluginPostMigrate");
        if (postMigrate == nullptr)
            continue;

        auto message = std::string {};
        if (postMigrate(connection, central, collectMessage, &message))
            continue;

        allSucceeded = false;
        if (logError)
            logError(std::format(
                "Plugin post-migrate failed ({}): {}", plugin.path.string(), message.empty() ? "(no message)" : message));
    }
    return allSucceeded;
}

} // namespace Lightweight::Tools
