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

#include <Lightweight/SqlMigration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
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
/// `MigrationBase` ctor); the destructor below resets the singleton so a
/// fixture instance going out of scope at end-of-test does not leave a
/// dangling pointer in the singleton's list. Without this reset the
/// next test's `FakeMigration` allocated at the same stack address is
/// (incorrectly) reported as a duplicate by `AddMigration`'s lookup.
class FakeMigration: public Lightweight::SqlMigration::MigrationBase
{
  public:
    FakeMigration(Lightweight::SqlMigration::MigrationTimestamp ts, std::string_view title):
        Lightweight::SqlMigration::MigrationBase(ts, title)
    {
    }

    ~FakeMigration() override
    {
        Lightweight::SqlMigration::MigrationManager::GetInstance().RemoveAllMigrations();
        Lightweight::SqlMigration::MigrationManager::GetInstance().RemoveAllReleases();
    }

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
    pluginManager->RegisterRelease("1.0.0", Lightweight::SqlMigration::MigrationTimestamp { 20260101000000ULL });

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
