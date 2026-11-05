// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the file `dbtool new-migration` creates.

#include "../../tools/dbtool/NewMigration.hpp"

#include <Lightweight/Utils.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using Lightweight::Tools::MakeNewMigrationFile;
using Lightweight::Tools::NextMigrationTimestamp;
using Lightweight::Tools::ParseMigrationFileTimestamp;
using Lightweight::Tools::WriteNewMigrationFile;
using namespace std::chrono_literals;
using namespace std::string_view_literals;

namespace
{

/// 2026-01-26 14:30:52 UTC.
constexpr auto SampleTime = std::chrono::sys_days { std::chrono::year { 2026 } / 1 / 26 } + 14h + 30min + 52s;

} // namespace

TEST_CASE("new-migration: the file name carries the timestamp and a slug of the title", "[dbtool][new-migration]")
{
    auto const file = MakeNewMigrationFile("Add users table!", SampleTime);
    REQUIRE(file.has_value());
    CHECK(file.value().fileName == "20260126143052_add_users_table.cpp");
}

TEST_CASE("new-migration: a title without ASCII letters or digits has no slug", "[dbtool][new-migration]")
{
    auto const file = MakeNewMigrationFile("Таблица", SampleTime);
    REQUIRE(file.has_value());
    CHECK(file.value().fileName == "20260126143052.cpp");
}

TEST_CASE("new-migration: a long title does not produce a long file name", "[dbtool][new-migration]")
{
    auto const file = MakeNewMigrationFile(std::string(500, 'a'), SampleTime);
    REQUIRE(file.has_value());
    CHECK(file.value().fileName == "20260126143052_" + std::string(64, 'a') + ".cpp");
}

TEST_CASE("new-migration: the source declares the migration with an escaped title", "[dbtool][new-migration]")
{
    auto const file = MakeNewMigrationFile(R"(Say "hi" to C:\temp)", SampleTime);
    REQUIRE(file.has_value());
    CHECK(file.value().source.contains(R"(LIGHTWEIGHT_SQL_MIGRATION(20260126143052, "Say \"hi\" to C:\\temp"))"));
    CHECK(file.value().source.contains("#include <Lightweight/SqlMigration.hpp>"));
    CHECK(file.value().source.contains("static_cast<void>(plan);"));
}

TEST_CASE("new-migration: a blank title or a control character is refused", "[dbtool][new-migration]")
{
    CHECK(!MakeNewMigrationFile("", SampleTime).has_value());
    CHECK(!MakeNewMigrationFile(" \t ", SampleTime).has_value());
    CHECK(!MakeNewMigrationFile("two\nlines", SampleTime).has_value());
}

TEST_CASE("new-migration: only a leading 14-digit date and time is a timestamp", "[dbtool][new-migration]")
{
    CHECK((ParseMigrationFileTimestamp("20260126143052_add_users.cpp") == SampleTime));
    CHECK((ParseMigrationFileTimestamp("20260126143052.cpp") == SampleTime));
    CHECK((ParseMigrationFileTimestamp("20260126143052") == SampleTime));
    CHECK(!ParseMigrationFileTimestamp("CMakeLists.txt").has_value());
    CHECK(!ParseMigrationFileTimestamp("2026012614305.cpp").has_value());
    CHECK(!ParseMigrationFileTimestamp("202601261430521.cpp").has_value());
    CHECK(!ParseMigrationFileTimestamp("20261326143052.cpp").has_value()); // month 13
    CHECK(!ParseMigrationFileTimestamp("20260230143052.cpp").has_value()); // February 30
    CHECK(!ParseMigrationFileTimestamp("20260126243052.cpp").has_value()); // hour 24
}

TEST_CASE("new-migration: the timestamp moves past every existing migration", "[dbtool][new-migration]")
{
    CHECK(NextMigrationTimestamp(SampleTime, {}) == SampleTime);

    auto const earlier = std::array { SampleTime - 1h };
    CHECK(NextMigrationTimestamp(SampleTime, earlier) == SampleTime);

    auto const sameSecond = std::array { SampleTime };
    CHECK(NextMigrationTimestamp(SampleTime, sameSecond) == SampleTime + 1s);

    // A clock behind the newest migration (another developer's, or this one's) must not sort before it.
    auto const later = std::array { SampleTime - 1h, SampleTime + 1h };
    CHECK(NextMigrationTimestamp(SampleTime, later) == SampleTime + 1h + 1s);

    // A carry across midnight still renders a valid timestamp.
    auto const endOfDay = std::chrono::sys_days { std::chrono::year { 2026 } / 12 / 31 } + 23h + 59min + 59s;
    auto const file = MakeNewMigrationFile("x", NextMigrationTimestamp(endOfDay, std::array { endOfDay }));
    REQUIRE(file.has_value());
    CHECK(file.value().fileName == "20270101000000_x.cpp");
}

TEST_CASE("new-migration: writing creates the file, and a second one sorts after it", "[dbtool][new-migration]")
{
    auto const directory = std::filesystem::temp_directory_path() / "lightweight_new_migration_test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    auto const cleanup = Lightweight::detail::Finally([&] { std::filesystem::remove_all(directory); });

    auto const first = WriteNewMigrationFile("First", directory, SampleTime);
    REQUIRE(first.has_value());
    CHECK(first.value().filename().string() == "20260126143052_first.cpp");

    auto stream = std::ifstream { first.value() };
    auto const content = std::string { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
    CHECK(content.contains(R"(LIGHTWEIGHT_SQL_MIGRATION(20260126143052, "First"))"));

    auto const second = WriteNewMigrationFile("Second", directory, SampleTime);
    REQUIRE(second.has_value());
    CHECK(second.value().filename().string() == "20260126143053_second.cpp");
}

TEST_CASE("new-migration: a missing directory is reported", "[dbtool][new-migration]")
{
    auto const missing = std::filesystem::temp_directory_path() / "lightweight_new_migration_no_such_dir";
    std::filesystem::remove_all(missing);
    auto const result = WriteNewMigrationFile("Title", missing, SampleTime);
    REQUIRE(!result.has_value());
    CHECK(result.error().contains("is not a directory"));
}
