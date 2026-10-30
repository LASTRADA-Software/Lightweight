// SPDX-License-Identifier: Apache-2.0
//
// A migration plugin links Lightweight statically, so it carries its own copy of the library's
// static state. These tests load a real plugin, make it use that state, and unload it again: what
// the copy allocated must be freed with it. The leak itself is caught by LeakSanitizer at exit,
// which the sanitizer CI job enables for this binary.

#include "ScopedEnvironmentVariable.hpp"
#include "Utils.hpp"

#include <Lightweight/SqlConnection.hpp>
#include <Lightweight/SqlMigration.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

#include <PluginLoader.hpp>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

using namespace Lightweight;

namespace
{

/// The post-migrate entry point that `LIGHTWEIGHT_MIGRATION_PLUGIN_POSTMIGRATE` exports.
using PostMigrateHook = bool(SqlConnection&,
                             SqlMigration::MigrationManager&,
                             SqlMigration::PluginHookErrorSink,
                             void*) noexcept;

/// Stores the message a plugin hook reports, for the test to show on failure.
void RecordHookError(void* context, char const* message) noexcept
{
    *static_cast<std::string*>(context) = message != nullptr ? message : "(no message)";
}

/// @return The path of the running test executable, or an empty path if it cannot be determined.
std::filesystem::path ExecutablePath()
{
    auto error = std::error_code {};
#if defined(_WIN32)
    auto buffer = std::wstring(32768, L'\0');
    auto const length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path { buffer.substr(0, length) };
#elif defined(__APPLE__)
    auto size = uint32_t { 0 };
    _NSGetExecutablePath(nullptr, &size);
    auto buffer = std::string(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};
    return std::filesystem::weakly_canonical(buffer.c_str(), error);
#else
    return std::filesystem::read_symlink("/proc/self/exe", error);
#endif
}

/// Finds the post-migrate fixture plugin.
///
/// The build tree path baked in at compile time is used when it exists. The CI database legs run an
/// installed package instead of the build tree; there the plugin lives in `share/Lightweight/tests/plugins`
/// next to the `bin` directory holding the test executable.
///
/// @return The plugin path, or an empty path if neither location holds the plugin.
std::filesystem::path FindPostMigratePlugin()
{
    auto const buildTreePath = std::filesystem::path { LIGHTWEIGHT_TEST_POST_MIGRATE_PLUGIN };
    if (std::filesystem::exists(buildTreePath))
        return buildTreePath;

    auto const executable = ExecutablePath();
    if (executable.empty())
        return {};
    auto const installed =
        executable.parent_path().parent_path() / "share" / "Lightweight" / "tests" / "plugins" / buildTreePath.filename();
    return std::filesystem::exists(installed) ? installed : std::filesystem::path {};
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture,
                 "Plugin unload frees what the plugin's copy of the library cached on connect",
                 "[PluginUnload]")
{
    auto const pluginPath = FindPostMigratePlugin();
    if (pluginPath.empty())
        SKIP(std::format("post-migrate fixture plugin not found at {} or in the installed share/ layout",
                         LIGHTWEIGHT_TEST_POST_MIGRATE_PLUGIN));

    auto const logPath =
        std::filesystem::temp_directory_path()
        / std::format("lightweight-plugin-unload-{}.log", std::chrono::steady_clock::now().time_since_epoch().count());
    // The plugin's post-migrate hook opens a second connection from inside the plugin, which records
    // the driver's text entry in the plugin's own copy of the connection cache.
    auto const logVariable = ScopedEnvironmentVariable { "LIGHTWEIGHT_TEST_POST_MIGRATE_LOG", logPath.string() };

    auto central = SqlMigration::MigrationManager {};
    central.CreateMigrationHistory();

    auto hookError = std::string {};
    {
        auto const plugin = Tools::PluginLoader { pluginPath };
        auto* const hook = plugin.GetFunction<PostMigrateHook>("LightweightMigrationPluginPostMigrate");
        auto connection = SqlConnection {};
        CHECK(hook(connection, central, &RecordHookError, &hookError));
    } // Unloads the library together with its static state.
    central.CloseDataMapper();

    INFO(hookError);
    auto log = std::ifstream { logPath };
    auto const logText = std::string { std::istreambuf_iterator<char> { log }, std::istreambuf_iterator<char> {} };
    log.close();
    std::filesystem::remove(logPath);
    CHECK(logText.starts_with("post-migrate pending=0 lock="));
}
