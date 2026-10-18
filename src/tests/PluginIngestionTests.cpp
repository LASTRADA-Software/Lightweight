// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for `Lightweight::Tools::IngestPlugins`, the DI seam used by
// both `dbtool` and `dbtool-gui` to load migration plugins. The
// `PluginLoaderFn` callback is the injection point that lets each test
// substitute a stub for the real `PluginLoader`; the merge step is
// exercised against locally-constructed `MigrationManager` instances
// (the class is freely constructible — `GetInstance()` is only one of
// many possible instances in the test process).
//
// `DiscoverPlugins` is unit-tested elsewhere (`PluginDiscoveryTests.cpp`);
// here we verify only that `IngestPlugins` correctly consumes its output
// and orchestrates the load + merge + logging contract.

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <PluginIngestion.hpp>

namespace
{

/// Per-test scratch root mirroring the helper in `PluginDiscoveryTests`.
/// Duplicated rather than shared because pulling the helper into a common
/// header would force every test TU to inherit the same include surface,
/// and these tests already depend on different bits of the codebase.
class ScopedTempTree
{
  public:
    ScopedTempTree():
        _root(std::filesystem::temp_directory_path()
              / std::format("lightweight-pluginingest-{}", std::chrono::steady_clock::now().time_since_epoch().count()))
    {
        std::filesystem::create_directories(_root);
    }

    ~ScopedTempTree()
    {
        std::error_code ec;
        std::filesystem::remove_all(_root, ec);
    }

    ScopedTempTree(ScopedTempTree const&) = delete;
    ScopedTempTree& operator=(ScopedTempTree const&) = delete;
    ScopedTempTree(ScopedTempTree&&) = delete;
    ScopedTempTree& operator=(ScopedTempTree&&) = delete;

    [[nodiscard]] std::filesystem::path Dir(std::string_view name) const
    {
        auto const p = _root / name;
        std::filesystem::create_directories(p);
        return p;
    }

  private:
    std::filesystem::path _root;
};

/// Creates a zero-byte file at `path` and pins its mtime. Plugin
/// extensions only — `DiscoverPlugins` filters everything else.
void TouchPluginFile(std::filesystem::path const& path, std::filesystem::file_time_type mtime)
{
    {
        std::ofstream os(path, std::ios::binary | std::ios::trunc);
        os << "fake plugin";
    }
    std::filesystem::last_write_time(path, mtime);
}

[[nodiscard]] std::filesystem::file_time_type Baseline()
{
    return std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
}

/// Minimal `MigrationBase` subclass usable as a fixture pointer for
/// `AddMigration`. Constructing one auto-registers it with
/// `MigrationManager::GetInstance()` (a hard-coded side effect of the
/// `MigrationBase` ctor) and `~MigrationBase` unregisters it again, so a
/// fixture instance going out of scope at end-of-test leaves no dangling
/// pointer in the singleton's list for the next test to trip over.
class FakeMigration: public Lightweight::SqlMigration::MigrationBase
{
  public:
    FakeMigration(Lightweight::SqlMigration::MigrationTimestamp ts, std::string_view title):
        Lightweight::SqlMigration::MigrationBase(ts, title)
    {
    }

    ~FakeMigration() override = default;

    FakeMigration(FakeMigration const&) = delete;
    FakeMigration& operator=(FakeMigration const&) = delete;
    FakeMigration(FakeMigration&&) = delete;
    FakeMigration& operator=(FakeMigration&&) = delete;

    void Up(Lightweight::SqlMigrationQueryBuilder& /*plan*/) const override
    {
        // No-op — the orchestration only stores pointers; nothing
        // dispatches `Up` during the tests.
    }
};

} // namespace

TEST_CASE("IngestPlugins — loader invoked once per resolved plugin", "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dirOld = tree.Dir("old");
    auto const dirNew = tree.Dir("new");

    auto const base = Baseline();
    TouchPluginFile(dirOld / "MyPlugin.dll", base);
    TouchPluginFile(dirNew / "MyPlugin.dll", base + std::chrono::seconds(60));

    Lightweight::SqlMigration::MigrationManager central;
    std::vector<std::filesystem::path> callPaths;

    auto loader = [&](std::filesystem::path const& path) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        callPaths.push_back(path);
        return Lightweight::Tools::LoadedPlugin {
            .path = path,
            .handle = std::make_shared<int>(0),
            .pluginManager = nullptr, // no merge step to verify here
        };
    };

    std::vector<std::filesystem::path> dirs { dirOld, dirNew };
    auto const loaded = Lightweight::Tools::IngestPlugins(dirs, loader, central);

    REQUIRE(loaded.size() == 1);
    REQUIRE(callPaths.size() == 1);
    CHECK(callPaths.front().parent_path() == dirNew);
}

TEST_CASE("IngestPlugins — nullopt from loader is a silent skip", "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("plugins");
    TouchPluginFile(dir / "RealPlugin.dll", Baseline());
    TouchPluginFile(dir / "PassthroughPlugin.dll", Baseline());

    Lightweight::SqlMigration::MigrationManager central;
    std::vector<std::string> errors;

    auto loader = [&](std::filesystem::path const& path) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        if (path.filename() == "PassthroughPlugin.dll")
            return std::nullopt;
        return Lightweight::Tools::LoadedPlugin {
            .path = path,
            .handle = std::make_shared<int>(0),
            .pluginManager = nullptr,
        };
    };

    Lightweight::Tools::PluginIngestOptions opts;
    opts.logError = [&](std::string_view m) {
        errors.emplace_back(m);
    };

    std::vector<std::filesystem::path> dirs { dir };
    auto const loaded = Lightweight::Tools::IngestPlugins(dirs, loader, central, opts);

    REQUIRE(loaded.size() == 1);
    CHECK(loaded.front().path.filename() == std::filesystem::path("RealPlugin.dll"));
    CHECK(errors.empty());
}

TEST_CASE("IngestPlugins — loader exception rethrows when throwOnLoadError is true", "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("plugins");
    TouchPluginFile(dir / "BadPlugin.dll", Baseline());

    Lightweight::SqlMigration::MigrationManager central;
    std::string errorLine;

    auto loader = [](std::filesystem::path const& /*path*/) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        throw std::runtime_error("boom");
    };

    Lightweight::Tools::PluginIngestOptions opts;
    opts.logError = [&](std::string_view m) {
        errorLine = m;
    };
    opts.throwOnLoadError = true;

    std::vector<std::filesystem::path> dirs { dir };
    REQUIRE_THROWS_AS(Lightweight::Tools::IngestPlugins(dirs, loader, central, opts), std::runtime_error);
    CHECK(errorLine.contains("BadPlugin.dll"));
    CHECK(errorLine.contains("boom"));
}

TEST_CASE("IngestPlugins — loader exception is swallowed when throwOnLoadError is false", "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("plugins");
    TouchPluginFile(dir / "APlugin.dll", Baseline());
    TouchPluginFile(dir / "BPlugin.dll", Baseline() + std::chrono::seconds(5));

    Lightweight::SqlMigration::MigrationManager central;
    int errorCount = 0;

    auto loader = [](std::filesystem::path const& path) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        if (path.filename() == "APlugin.dll")
            throw std::runtime_error("APlugin.dll is poisoned");
        return Lightweight::Tools::LoadedPlugin {
            .path = path,
            .handle = std::make_shared<int>(0),
            .pluginManager = nullptr,
        };
    };

    Lightweight::Tools::PluginIngestOptions opts;
    opts.logError = [&](std::string_view) {
        ++errorCount;
    };
    opts.throwOnLoadError = false;

    std::vector<std::filesystem::path> dirs { dir };
    auto const loaded = Lightweight::Tools::IngestPlugins(dirs, loader, central, opts);

    REQUIRE(loaded.size() == 1);
    CHECK(loaded.front().path.filename() == std::filesystem::path("BPlugin.dll"));
    CHECK(errorCount == 1);
}

TEST_CASE("IngestPlugins — duplicate handle is rejected", "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("plugins");
    TouchPluginFile(dir / "FirstPlugin.dll", Baseline());
    TouchPluginFile(dir / "SecondPlugin.dll", Baseline() + std::chrono::seconds(5));

    Lightweight::SqlMigration::MigrationManager central;
    // Both calls hand back the *same* shared_ptr — simulating an OS-level
    // symlink collision the filename-based dedup cannot see.
    auto sharedHandle = std::make_shared<int>(42);
    std::string errorLine;

    auto loader = [&](std::filesystem::path const& path) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        return Lightweight::Tools::LoadedPlugin {
            .path = path,
            .handle = sharedHandle,
            .pluginManager = nullptr,
        };
    };

    Lightweight::Tools::PluginIngestOptions opts;
    opts.logError = [&](std::string_view m) {
        errorLine = m;
    };
    opts.throwOnLoadError = false;

    std::vector<std::filesystem::path> dirs { dir };
    auto const loaded = Lightweight::Tools::IngestPlugins(dirs, loader, central, opts);

    REQUIRE(loaded.size() == 1);
    CHECK(errorLine.contains("duplicate handle"));
}

TEST_CASE("IngestPlugins — releases and migrations propagate to central", "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("plugins");
    TouchPluginFile(dir / "MainPlugin.dll", Baseline());

    Lightweight::SqlMigration::MigrationManager central;

    // A local "plugin manager" — populated with a release and a migration
    // that the orchestration must copy across.
    auto pluginManager = std::make_unique<Lightweight::SqlMigration::MigrationManager>();
    FakeMigration migration { Lightweight::SqlMigration::MigrationTimestamp { 20260101000000ULL }, "fixture" };
    pluginManager->AddMigration(&migration);
    pluginManager->RegisterRelease(
        "1.0.0", Lightweight::SqlMigration::MigrationTimestamp { 20260101000000ULL }, { .requiresBackup = true });

    auto loader = [&](std::filesystem::path const& path) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        return Lightweight::Tools::LoadedPlugin {
            .path = path,
            .handle = std::make_shared<int>(0),
            .pluginManager = pluginManager.get(),
        };
    };

    std::vector<std::filesystem::path> dirs { dir };
    auto const loaded = Lightweight::Tools::IngestPlugins(dirs, loader, central);

    REQUIRE(loaded.size() == 1);

    auto const& centralMigrations = central.GetAllMigrations();
    REQUIRE(centralMigrations.size() == 1);
    CHECK(centralMigrations.front()->GetTimestamp() == Lightweight::SqlMigration::MigrationTimestamp { 20260101000000ULL });

    auto const& releases = central.GetAllReleases();
    REQUIRE(releases.size() == 1);
    CHECK(releases.front().version == "1.0.0");
    CHECK(releases.front().requiresBackup);
}

TEST_CASE("IngestPlugins — pluginManager == &central is a no-op merge", "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("plugins");
    TouchPluginFile(dir / "SelfPlugin.dll", Baseline());

    Lightweight::SqlMigration::MigrationManager central;
    FakeMigration preexisting { Lightweight::SqlMigration::MigrationTimestamp { 20260201000000ULL }, "pre" };
    central.AddMigration(&preexisting);

    auto loader = [&](std::filesystem::path const& path) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        return Lightweight::Tools::LoadedPlugin {
            .path = path,
            .handle = std::make_shared<int>(0),
            .pluginManager = &central, // loader (incorrectly) hands back the host manager
        };
    };

    std::vector<std::filesystem::path> dirs { dir };
    auto const loaded = Lightweight::Tools::IngestPlugins(dirs, loader, central);

    REQUIRE(loaded.size() == 1);
    // The orchestration must NOT re-register the pre-existing migration.
    CHECK(central.GetAllMigrations().size() == 1);
}

TEST_CASE("IngestPlugins — logShadowed wired through to DiscoverPlugins", "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dirOld = tree.Dir("old");
    auto const dirNew = tree.Dir("new");
    auto const base = Baseline();
    TouchPluginFile(dirOld / "TwinPlugin.dll", base);
    TouchPluginFile(dirNew / "TwinPlugin.dll", base + std::chrono::seconds(30));

    Lightweight::SqlMigration::MigrationManager central;
    int shadowedCount = 0;

    auto loader = [](std::filesystem::path const& path) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        return Lightweight::Tools::LoadedPlugin {
            .path = path,
            .handle = std::make_shared<int>(0),
            .pluginManager = nullptr,
        };
    };

    Lightweight::Tools::PluginIngestOptions opts;
    opts.logShadowed = [&](std::string_view) {
        ++shadowedCount;
    };

    std::vector<std::filesystem::path> dirs { dirOld, dirNew };
    auto const loaded = Lightweight::Tools::IngestPlugins(dirs, loader, central, opts);

    REQUIRE(loaded.size() == 1);
    CHECK(shadowedCount == 1);
}

TEST_CASE("IngestPlugins — logLoading fires per successfully-loaded plugin", "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("plugins");
    TouchPluginFile(dir / "APlugin.dll", Baseline());
    TouchPluginFile(dir / "BPlugin.dll", Baseline() + std::chrono::seconds(1));
    TouchPluginFile(dir / "CPlugin.dll", Baseline() + std::chrono::seconds(2));

    Lightweight::SqlMigration::MigrationManager central;
    int loadingCount = 0;

    auto loader = [&](std::filesystem::path const& path) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        return Lightweight::Tools::LoadedPlugin {
            .path = path,
            .handle = std::make_shared<int>(0),
            .pluginManager = nullptr,
        };
    };

    Lightweight::Tools::PluginIngestOptions opts;
    opts.logLoading = [&](std::string_view) {
        ++loadingCount;
    };

    std::vector<std::filesystem::path> dirs { dir };
    auto const loaded = Lightweight::Tools::IngestPlugins(dirs, loader, central, opts);

    CHECK(loaded.size() == 3);
    CHECK(loadingCount == 3);
}

TEST_CASE("IngestPlugins — LoadedPlugin::TryGetFunction wraps tryGetSymbol", "[PluginIngestion]")
{
    // A real function whose address we can verify the lookup returns.
    static auto target = []() -> int {
        return 42;
    };

    Lightweight::Tools::LoadedPlugin plugin;
    plugin.tryGetSymbol = [](std::string const& name) -> Lightweight::Tools::PluginLoader::GenericFunctionPointer {
        if (name == "fortyTwo")
            return reinterpret_cast<Lightweight::Tools::PluginLoader::GenericFunctionPointer>(+target);
        return nullptr;
    };

    auto* found = plugin.TryGetFunction<int()>("fortyTwo");
    REQUIRE(found != nullptr);
    CHECK(found() == 42);

    auto* missing = plugin.TryGetFunction<int()>("nope");
    CHECK(missing == nullptr);

    // Empty tryGetSymbol returns nullptr without crashing.
    Lightweight::Tools::LoadedPlugin empty;
    CHECK(empty.TryGetFunction<int()>("anything") == nullptr);
}

namespace
{

/// Per-symbol call counters for the hook stubs below. Hooks are plain
/// function pointers, so the counters cannot be captured state.
struct HookCalls
{
    int postInit = 0;
    int postMigrate = 0;
};

HookCalls& RecordedHookCalls()
{
    static HookCalls calls;
    return calls;
}

void RecordingPostInit(Lightweight::SqlConnection& /*connection*/, Lightweight::SqlMigration::MigrationManager& /*manager*/)
{
    ++RecordedHookCalls().postInit;
}

void RegisteringPostInit(Lightweight::SqlConnection& /*connection*/, Lightweight::SqlMigration::MigrationManager& manager)
{
    manager.AddPreMigrateStep("Plugin step", {}, [](auto&, auto&, auto&, auto) {});
}

bool RecordingPostMigrate(Lightweight::SqlConnection& /*connection*/,
                          Lightweight::SqlMigration::MigrationManager& /*manager*/,
                          Lightweight::SqlMigration::PostMigrateErrorSink /*reportError*/,
                          void* /*errorContext*/) noexcept
{
    ++RecordedHookCalls().postMigrate;
    return true;
}

/// Stands in for a hook body that threw: the exported entry point reports and returns false.
bool FailingPostMigrate(Lightweight::SqlConnection& /*connection*/,
                        Lightweight::SqlMigration::MigrationManager& /*manager*/,
                        Lightweight::SqlMigration::PostMigrateErrorSink reportError,
                        void* errorContext) noexcept
{
    ++RecordedHookCalls().postMigrate;
    reportError(errorContext, "seed failed");
    return false;
}

/// Builds a plugin whose symbol lookup resolves exactly the given symbols.
Lightweight::Tools::LoadedPlugin MakeHookPlugin(
    std::string const& path, std::map<std::string, Lightweight::Tools::PluginLoader::GenericFunctionPointer> symbols)
{
    Lightweight::Tools::LoadedPlugin plugin;
    plugin.path = path;
    plugin.tryGetSymbol =
        [symbols = std::move(symbols)](std::string const& name) -> Lightweight::Tools::PluginLoader::GenericFunctionPointer {
        auto const found = symbols.find(name);
        return found != symbols.end() ? found->second : nullptr;
    };
    return plugin;
}

template <typename Function>
Lightweight::Tools::PluginLoader::GenericFunctionPointer AsSymbol(Function* function)
{
    return reinterpret_cast<Lightweight::Tools::PluginLoader::GenericFunctionPointer>(function);
}

} // namespace

TEST_CASE("RunPluginPostMigrateHooks — runs every exporting plugin and skips the others", "[PluginIngestion]")
{
    RecordedHookCalls() = {};
    auto const plugins = std::vector {
        MakeHookPlugin("a", { { "LightweightMigrationPluginPostMigrate", AsSymbol(&RecordingPostMigrate) } }),
        MakeHookPlugin("without-hook", {}),
        MakeHookPlugin("b", { { "LightweightMigrationPluginPostMigrate", AsSymbol(&RecordingPostMigrate) } }),
    };
    Lightweight::SqlConnection connection { std::nullopt };
    Lightweight::SqlMigration::MigrationManager central;
    std::vector<std::string> errors;

    auto const succeeded = Lightweight::Tools::RunPluginPostMigrateHooks(
        plugins, connection, central, [&errors](std::string_view message) { errors.emplace_back(message); });

    CHECK(succeeded);
    CHECK(RecordedHookCalls().postMigrate == 2);
    CHECK(errors.empty());
}

TEST_CASE("RunPluginPostMigrateHooks — a failing hook fails the run but the others still run", "[PluginIngestion]")
{
    RecordedHookCalls() = {};
    auto const plugins = std::vector {
        MakeHookPlugin("failing", { { "LightweightMigrationPluginPostMigrate", AsSymbol(&FailingPostMigrate) } }),
        MakeHookPlugin("healthy", { { "LightweightMigrationPluginPostMigrate", AsSymbol(&RecordingPostMigrate) } }),
    };
    Lightweight::SqlConnection connection { std::nullopt };
    Lightweight::SqlMigration::MigrationManager central;
    std::vector<std::string> errors;

    auto const succeeded = Lightweight::Tools::RunPluginPostMigrateHooks(
        plugins, connection, central, [&errors](std::string_view message) { errors.emplace_back(message); });

    CHECK_FALSE(succeeded);
    CHECK(RecordedHookCalls().postMigrate == 2);
    REQUIRE(errors.size() == 1);
    CHECK(errors.front().contains("failing"));
    CHECK(errors.front().contains("seed failed"));
}

TEST_CASE("RunPluginPostMigrateHooks and RunPluginPostInitHooks resolve only their own symbol", "[PluginIngestion]")
{
    RecordedHookCalls() = {};
    auto const plugins = std::vector {
        MakeHookPlugin("both",
                       { { "LightweightMigrationPluginPostInit", AsSymbol(&RecordingPostInit) },
                         { "LightweightMigrationPluginPostMigrate", AsSymbol(&RecordingPostMigrate) } }),
    };
    Lightweight::SqlConnection connection { std::nullopt };
    Lightweight::SqlMigration::MigrationManager central;

    CHECK(Lightweight::Tools::RunPluginPostMigrateHooks(plugins, connection, central, {}));
    CHECK(RecordedHookCalls().postInit == 0);
    CHECK(RecordedHookCalls().postMigrate == 1);

    Lightweight::Tools::RunPluginPostInitHooks(plugins, connection, central, {});
    CHECK(RecordedHookCalls().postInit == 1);
    CHECK(RecordedHookCalls().postMigrate == 1);
}

TEST_CASE("RunPluginPostInitHooks — a second run replaces the steps of the first", "[PluginIngestion]")
{
    auto const plugins = std::vector {
        MakeHookPlugin("a", { { "LightweightMigrationPluginPostInit", AsSymbol(&RegisteringPostInit) } }),
    };
    Lightweight::SqlConnection connection { std::nullopt };
    Lightweight::SqlMigration::MigrationManager central;
    auto errors = std::vector<std::string> {};
    auto const logError = [&errors](std::string_view message) {
        errors.emplace_back(message);
    };

    Lightweight::Tools::RunPluginPostInitHooks(plugins, connection, central, logError);
    Lightweight::Tools::RunPluginPostInitHooks(plugins, connection, central, logError);

    CHECK(errors.empty());
    CHECK(central.GetPreMigrateSteps().size() == 1);
}

TEST_CASE("PluginsWithPostInitHook lists exactly the plugins that export a post-init hook", "[PluginIngestion]")
{
    auto const plugins = std::vector {
        MakeHookPlugin("seeding", { { "LightweightMigrationPluginPostInit", AsSymbol(&RecordingPostInit) } }),
        MakeHookPlugin("post-migrate-only",
                       { { "LightweightMigrationPluginPostMigrate", AsSymbol(&RecordingPostMigrate) } }),
        MakeHookPlugin("without-hook", {}),
        MakeHookPlugin("bridging", { { "LightweightMigrationPluginPostInit", AsSymbol(&RecordingPostInit) } }),
    };

    CHECK(Lightweight::Tools::PluginsWithPostInitHook(plugins)
          == std::vector<std::filesystem::path> { "seeding", "bridging" });
    CHECK(Lightweight::Tools::PluginsWithPostInitHook(std::span<Lightweight::Tools::LoadedPlugin const> {}).empty());

    auto unresolvable = Lightweight::Tools::LoadedPlugin {};
    unresolvable.path = "no-symbol-lookup";
    CHECK(Lightweight::Tools::PluginsWithPostInitHook(std::vector { unresolvable }).empty());
}

TEST_CASE("UnloadPlugins — central lets go of plugin state before the plugin is unloaded", "[PluginIngestion]")
{
    Lightweight::SqlMigration::MigrationManager central;
    FakeMigration migration { Lightweight::SqlMigration::MigrationTimestamp { 20260301000000ULL }, "plugin" };
    central.AddMigration(&migration);
    central.RegisterRelease("1.0.0", Lightweight::SqlMigration::MigrationTimestamp { 20260301000000ULL });
    central.SetCompatPolicy([](Lightweight::SqlMigration::MigrationBase const&) { return std::set<std::string> {}; });
    central.AddPreMigrateStep("Plugin step", {}, [](auto&, auto&, auto&, auto) {});

    // What central still held when the plugin's handle was released, i.e. its library unloaded.
    struct StateAtUnload
    {
        bool unloaded = false;
        bool hadCompatPolicy = false;
        std::size_t migrations = 0;
        std::size_t releases = 0;
        std::size_t preMigrateSteps = 0;
    } stateAtUnload;

    auto const recordStateAtUnload = [&](void*) {
        stateAtUnload = { .unloaded = true,
                          .hadCompatPolicy = static_cast<bool>(central.GetCompatPolicy()),
                          .migrations = central.GetAllMigrations().size(),
                          .releases = central.GetAllReleases().size(),
                          .preMigrateSteps = central.GetPreMigrateSteps().size() };
    };
    auto plugins = std::vector<Lightweight::Tools::LoadedPlugin> {};
    plugins.push_back(Lightweight::Tools::LoadedPlugin {
        .path = "CompatPolicyPlugin.dll",
        .handle = std::shared_ptr<void>(nullptr, recordStateAtUnload),
    });

    Lightweight::Tools::UnloadPlugins(plugins, central);

    CHECK(plugins.empty());
    REQUIRE(stateAtUnload.unloaded);
    CHECK_FALSE(stateAtUnload.hadCompatPolicy);
    CHECK(stateAtUnload.migrations == 0);
    CHECK(stateAtUnload.releases == 0);
    CHECK(stateAtUnload.preMigrateSteps == 0);
}

TEST_CASE("IngestPlugins — a failed load unloads the plugins loaded before it only after central lets go of them",
          "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("plugins");
    TouchPluginFile(dir / "PolicyPlugin.dll", Baseline());
    TouchPluginFile(dir / "BrokenPlugin.dll", Baseline());

    Lightweight::SqlMigration::MigrationManager central;
    auto loaderCalls = 0;
    auto unloaded = false;
    auto hadCompatPolicyAtUnload = false;

    // The first plugin loaded installs a compat policy as its static initializer would; the second, either one, fails.
    auto loader = [&](std::filesystem::path const& path) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        if (++loaderCalls == 2)
            throw std::runtime_error("not a valid shared library");
        central.SetCompatPolicy([](Lightweight::SqlMigration::MigrationBase const&) { return std::set<std::string> {}; });
        auto const recordUnload = [&](void*) {
            unloaded = true;
            hadCompatPolicyAtUnload = static_cast<bool>(central.GetCompatPolicy());
        };
        return Lightweight::Tools::LoadedPlugin {
            .path = path,
            .handle = std::shared_ptr<void>(nullptr, recordUnload),
        };
    };

    std::vector<std::filesystem::path> dirs { dir };
    REQUIRE_THROWS_AS(Lightweight::Tools::IngestPlugins(dirs, loader, central), std::runtime_error);
    REQUIRE(loaderCalls == 2);
    REQUIRE(unloaded);
    CHECK_FALSE(hadCompatPolicyAtUnload);
}

TEST_CASE("IngestPlugins — a failed merge unloads the plugin only after central lets go of it", "[PluginIngestion]")
{
    ScopedTempTree const tree;
    auto const dir = tree.Dir("plugins");
    TouchPluginFile(dir / "DuplicateReleasePlugin.dll", Baseline());

    Lightweight::SqlMigration::MigrationManager central;
    central.RegisterRelease("1.0.0", Lightweight::SqlMigration::MigrationTimestamp { 20260401000000ULL });
    auto unloaded = false;
    auto hadCompatPolicyAtUnload = false;

    // The plugin installs a compat policy as its static initializer would, then declares a release central already has.
    auto pluginManager = std::make_unique<Lightweight::SqlMigration::MigrationManager>();
    pluginManager->RegisterRelease("1.0.0", Lightweight::SqlMigration::MigrationTimestamp { 20260402000000ULL });
    auto loader = [&](std::filesystem::path const& path) -> std::optional<Lightweight::Tools::LoadedPlugin> {
        central.SetCompatPolicy([](Lightweight::SqlMigration::MigrationBase const&) { return std::set<std::string> {}; });
        auto const recordUnload = [&](void*) {
            unloaded = true;
            hadCompatPolicyAtUnload = static_cast<bool>(central.GetCompatPolicy());
        };
        auto plugin = Lightweight::Tools::LoadedPlugin {};
        plugin.path = path;
        plugin.handle = std::shared_ptr<void>(nullptr, recordUnload);
        plugin.pluginManager = pluginManager.get();
        return plugin;
    };

    std::vector<std::filesystem::path> dirs { dir };
    REQUIRE_THROWS_AS(Lightweight::Tools::IngestPlugins(dirs, loader, central), std::runtime_error);
    REQUIRE(unloaded);
    CHECK_FALSE(hadCompatPolicyAtUnload);
}
