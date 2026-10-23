// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for `Lightweight::Config::ResolveConnectionTarget`: which source supplies dbtool's
// connection, whether the selected profile's settings apply, and whether a writing command may
// use the result.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <format>
#include <string>
#include <string_view>

#include <Config/ConnectionTarget.hpp>

namespace Cfg = Lightweight::Config;
using Source = Cfg::ConnectionTargetSource;
using Cfg::ConfigSource;

namespace
{

/// One row of the precedence table.
struct TargetCase
{
    std::string_view description;
    Cfg::ConnectionTargetInputs inputs;
    Source expectedSource;
    bool expectedProfileSettings;
    bool expectedWritable;
};

constexpr auto TargetCases = std::array {
    // --connection-string beats everything; a profile applies only when named.
    TargetCase { .description = "CLI string alone",
                 .inputs = { .commandLineConnectionString = true },
                 .expectedSource = Source::CommandLine,
                 .expectedProfileSettings = false,
                 .expectedWritable = true },
    TargetCase { .description = "CLI string over a discovered default profile and env",
                 .inputs = { .commandLineConnectionString = true,
                             .configSource = ConfigSource::ExecutableDirectory,
                             .profileHasConnection = true,
                             .environmentConnectionString = true },
                 .expectedSource = Source::CommandLine,
                 .expectedProfileSettings = false,
                 .expectedWritable = true },
    TargetCase { .description = "CLI string with --config but no --profile",
                 .inputs = { .commandLineConnectionString = true,
                             .configSource = ConfigSource::Explicit,
                             .profileHasConnection = true },
                 .expectedSource = Source::CommandLine,
                 .expectedProfileSettings = false,
                 .expectedWritable = true },
    TargetCase { .description = "CLI string with --profile keeps the profile's settings",
                 .inputs = { .commandLineConnectionString = true,
                             .explicitProfile = true,
                             .configSource = ConfigSource::Explicit,
                             .profileHasConnection = true },
                 .expectedSource = Source::CommandLine,
                 .expectedProfileSettings = true,
                 .expectedWritable = true },

    // --profile, from --config or a discovered file, beats the environment.
    TargetCase { .description = "--profile from --config over env",
                 .inputs = { .explicitProfile = true,
                             .configSource = ConfigSource::Explicit,
                             .profileHasConnection = true,
                             .environmentConnectionString = true },
                 .expectedSource = Source::ExplicitProfile,
                 .expectedProfileSettings = true,
                 .expectedWritable = true },
    TargetCase { .description = "--profile from a discovered file over env",
                 .inputs = { .explicitProfile = true,
                             .configSource = ConfigSource::ExecutableDirectory,
                             .profileHasConnection = true,
                             .environmentConnectionString = true },
                 .expectedSource = Source::ExplicitProfile,
                 .expectedProfileSettings = true,
                 .expectedWritable = true },
    TargetCase {
        .description = "--profile from the user default file",
        .inputs = { .explicitProfile = true, .configSource = ConfigSource::UserDefault, .profileHasConnection = true },
        .expectedSource = Source::ExplicitProfile,
        .expectedProfileSettings = true,
        .expectedWritable = true },
    TargetCase { .description = "--profile without a connection falls through to env and keeps its settings",
                 .inputs = { .explicitProfile = true,
                             .configSource = ConfigSource::ExecutableDirectory,
                             .environmentConnectionString = true },
                 .expectedSource = Source::Environment,
                 .expectedProfileSettings = true,
                 .expectedWritable = true },

    // --config without --profile: its defaultProfile counts as explicit.
    TargetCase { .description = "--config default profile over env",
                 .inputs = { .configSource = ConfigSource::Explicit,
                             .profileHasConnection = true,
                             .environmentConnectionString = true },
                 .expectedSource = Source::ExplicitConfig,
                 .expectedProfileSettings = true,
                 .expectedWritable = true },
    TargetCase { .description = "--config default profile without env",
                 .inputs = { .configSource = ConfigSource::Explicit, .profileHasConnection = true },
                 .expectedSource = Source::ExplicitConfig,
                 .expectedProfileSettings = true,
                 .expectedWritable = true },
    TargetCase { .description = "--config default profile without a connection falls through to env",
                 .inputs = { .configSource = ConfigSource::Explicit, .environmentConnectionString = true },
                 .expectedSource = Source::Environment,
                 .expectedProfileSettings = true,
                 .expectedWritable = true },

    // The environment beats a discovered file's defaultProfile, whose settings then do not apply.
    TargetCase { .description = "env alone",
                 .inputs = { .environmentConnectionString = true },
                 .expectedSource = Source::Environment,
                 .expectedProfileSettings = false,
                 .expectedWritable = true },
    TargetCase { .description = "env over a default profile found next to the executable",
                 .inputs = { .configSource = ConfigSource::ExecutableDirectory,
                             .profileHasConnection = true,
                             .environmentConnectionString = true },
                 .expectedSource = Source::Environment,
                 .expectedProfileSettings = false,
                 .expectedWritable = true },
    TargetCase { .description = "env over the user default file's default profile",
                 .inputs = { .configSource = ConfigSource::UserDefault,
                             .profileHasConnection = true,
                             .environmentConnectionString = true },
                 .expectedSource = Source::Environment,
                 .expectedProfileSettings = false,
                 .expectedWritable = true },
    TargetCase { .description = "env with a discovered file whose default profile has no connection keeps its settings",
                 .inputs = { .configSource = ConfigSource::ExecutableDirectory, .environmentConnectionString = true },
                 .expectedSource = Source::Environment,
                 .expectedProfileSettings = true,
                 .expectedWritable = true },

    // A discovered defaultProfile is used only as a last resort, and never for writing.
    TargetCase { .description = "discovered default profile next to the executable",
                 .inputs = { .configSource = ConfigSource::ExecutableDirectory, .profileHasConnection = true },
                 .expectedSource = Source::DiscoveredConfig,
                 .expectedProfileSettings = true,
                 .expectedWritable = false },
    TargetCase { .description = "the user default file's default profile",
                 .inputs = { .configSource = ConfigSource::UserDefault, .profileHasConnection = true },
                 .expectedSource = Source::DiscoveredConfig,
                 .expectedProfileSettings = true,
                 .expectedWritable = false },

    // Nothing supplies a connection.
    TargetCase { .description = "nothing at all",
                 .inputs = {},
                 .expectedSource = Source::None,
                 .expectedProfileSettings = false,
                 .expectedWritable = false },
    TargetCase { .description = "a discovered default profile without a connection",
                 .inputs = { .configSource = ConfigSource::ExecutableDirectory },
                 .expectedSource = Source::None,
                 .expectedProfileSettings = true,
                 .expectedWritable = false },
    TargetCase { .description = "a profile flag without any config file and no env",
                 .inputs = { .explicitProfile = true, .profileHasConnection = true },
                 .expectedSource = Source::None,
                 .expectedProfileSettings = false,
                 .expectedWritable = false },
};

} // namespace

TEST_CASE("ResolveConnectionTarget follows the documented precedence", "[ConnectionTarget]")
{
    for (auto const& testCase: TargetCases)
    {
        INFO(testCase.description);
        auto const target = Cfg::ResolveConnectionTarget(testCase.inputs);
        CHECK(target.source == testCase.expectedSource);
        CHECK(target.appliesProfileSettings == testCase.expectedProfileSettings);
        CHECK(Cfg::AllowsWrites(target.source) == testCase.expectedWritable);
    }
}

TEST_CASE("DescribeConnectionTarget names server, database and DSN but no credentials", "[ConnectionTarget]")
{
    using Lightweight::SqlConnectionString;

    auto const mssql = Cfg::DescribeConnectionTarget(SqlConnectionString {
        "DRIVER={ODBC Driver 18 for SQL Server};Server=127.0.0.1,1444;Database=inventory;UID=app;PWD={se;cret}" });
    CHECK(mssql == "server 127.0.0.1,1444, database inventory");

    auto const postgres = Cfg::DescribeConnectionTarget(
        SqlConnectionString { "Driver={PostgreSQL Unicode};Host=db;Port=5436;Uid=u;Pwd=p;Database=test" });
    CHECK(postgres == "server db, port 5436, database test");

    auto const sqlite = Cfg::DescribeConnectionTarget(SqlConnectionString { "DRIVER=SQLite3;Database=D:/tmp/a.db" });
    CHECK(sqlite == "database D:/tmp/a.db");

    auto const dsn = Cfg::DescribeConnectionTarget(SqlConnectionString { "DSN=prod;UID=deploy;PWD=secret" });
    CHECK(dsn == "DSN prod");

    auto const serverOverHost = Cfg::DescribeConnectionTarget(SqlConnectionString { "Server=a;Host=b;Address=c" });
    CHECK(serverOverHost == "server a");

    auto const servername = Cfg::DescribeConnectionTarget(
        SqlConnectionString { "Driver={PostgreSQL Unicode};Servername=prod;Port=5432;Database=sales;Pwd=p" });
    CHECK(servername == "server prod, port 5432, database sales");

    auto const addr = Cfg::DescribeConnectionTarget(SqlConnectionString { "Driver=x;Addr=10.0.0.5,1433;Database=d" });
    CHECK(addr == "server 10.0.0.5,1433, database d");

    auto const networkAddress =
        Cfg::DescribeConnectionTarget(SqlConnectionString { "Driver=x;Network Address=db.example;UID=u;PWD=p" });
    CHECK(networkAddress == "server db.example");

    auto const fileDsn = Cfg::DescribeConnectionTarget(SqlConnectionString { "FILEDSN=D:/dsn/prod.dsn;PWD=secret" });
    CHECK(fileDsn == "DSN file D:/dsn/prod.dsn");

    auto const bare = Cfg::DescribeConnectionTarget(SqlConnectionString { "DRIVER=x;PWD=secret" });
    CHECK(bare == "a connection string without server, database or DSN");
    CHECK(!bare.contains("secret"));
}

TEST_CASE("FirstProfileFallbackNote tells a missing defaultProfile from an unknown one", "[ConnectionTarget]")
{
    CHECK(Cfg::FirstProfileFallbackNote("") == " (the first profile; the file sets no defaultProfile)");
    CHECK(Cfg::FirstProfileFallbackNote("prod")
          == " (the first profile; the file's defaultProfile 'prod' names no profile)");
}

TEST_CASE("IsProfileSource is true exactly when a profile supplies the connection", "[ConnectionTarget]")
{
    CHECK_FALSE(Cfg::IsProfileSource(Source::CommandLine));
    CHECK(Cfg::IsProfileSource(Source::ExplicitProfile));
    CHECK(Cfg::IsProfileSource(Source::ExplicitConfig));
    CHECK_FALSE(Cfg::IsProfileSource(Source::Environment));
    CHECK(Cfg::IsProfileSource(Source::DiscoveredConfig));
    CHECK_FALSE(Cfg::IsProfileSource(Source::None));
}
