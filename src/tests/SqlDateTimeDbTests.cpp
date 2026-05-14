// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/SqlDate.hpp>
#include <Lightweight/DataBinder/SqlDateTime.hpp>
#include <Lightweight/DataBinder/SqlTime.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace Lightweight;

// ================================================================================================
// SqlDate round-trips through a DATE column
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlDate: round-trip through a DATE column", "[SqlDate]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Dates").RequiredColumn("d", SqlColumnTypeDefinitions::Date {}); });

    auto const inputValue = SqlDate { std::chrono::year { 2026 }, std::chrono::month { 5 }, std::chrono::day { 6 } };
    stmt.Prepare(R"(INSERT INTO "Dates" ("d") VALUES (?))");
    (void) stmt.Execute(inputValue);

    auto const fetched = stmt.ExecuteDirectScalar<SqlDate>(R"(SELECT "d" FROM "Dates")");
    REQUIRE(fetched.has_value());
    if (fetched.has_value())
    {
        CHECK(*fetched == inputValue);
        CHECK(fetched->sqlValue.year == 2026);
        CHECK(fetched->sqlValue.month == 5);
        CHECK(fetched->sqlValue.day == 6);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDate: NULL fetch returns nullopt", "[SqlDate]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Dates").Column("d", SqlColumnTypeDefinitions::Date {}); });

    (void) stmt.ExecuteDirect(R"(INSERT INTO "Dates" ("d") VALUES (NULL))");

    auto const fetched = stmt.ExecuteDirectScalar<SqlDate>(R"(SELECT "d" FROM "Dates")");
    CHECK_FALSE(fetched.has_value());
}

// ================================================================================================
// SqlTime round-trips through a TIME column
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlTime: round-trip through a TIME column", "[SqlTime]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect(
        [](auto& migration) { migration.CreateTable("Times").RequiredColumn("t", SqlColumnTypeDefinitions::Time {}); });

    auto const inputValue = SqlTime { 13h, 14min, 15s };
    stmt.Prepare(R"(INSERT INTO "Times" ("t") VALUES (?))");
    (void) stmt.Execute(inputValue);

    auto const fetched = stmt.ExecuteDirectScalar<SqlTime>(R"(SELECT "t" FROM "Times")");
    REQUIRE(fetched.has_value());
    if (fetched.has_value())
    {
        CHECK(fetched->sqlValue.hour == 13);
        CHECK(fetched->sqlValue.minute == 14);
        CHECK(fetched->sqlValue.second == 15);
    }
}

// ================================================================================================
// SqlDateTime round-trips through a TIMESTAMP column
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "SqlDateTime: round-trip through a TIMESTAMP column", "[SqlDateTime]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Stamps").RequiredColumn("ts", SqlColumnTypeDefinitions::DateTime {});
    });

    auto const inputValue =
        SqlDateTime { std::chrono::year { 2026 },    std::chrono::month { 5 }, std::chrono::day { 6 }, 13h, 14min, 15s,
                      std::chrono::nanoseconds { 0 } };
    stmt.Prepare(R"(INSERT INTO "Stamps" ("ts") VALUES (?))");
    (void) stmt.Execute(inputValue);

    auto const fetched = stmt.ExecuteDirectScalar<SqlDateTime>(R"(SELECT "ts" FROM "Stamps")");
    REQUIRE(fetched.has_value());
    if (fetched.has_value())
    {
        CHECK(fetched->sqlValue.year == 2026);
        CHECK(fetched->sqlValue.month == 5);
        CHECK(fetched->sqlValue.day == 6);
        CHECK(fetched->sqlValue.hour == 13);
        CHECK(fetched->sqlValue.minute == 14);
        CHECK(fetched->sqlValue.second == 15);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlDateTime::Now produces a fetchable value", "[SqlDateTime]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Stamps").RequiredColumn("ts", SqlColumnTypeDefinitions::DateTime {});
    });

    auto const before = SqlDateTime::Now();
    stmt.Prepare(R"(INSERT INTO "Stamps" ("ts") VALUES (?))");
    (void) stmt.Execute(before);

    auto const fetched = stmt.ExecuteDirectScalar<SqlDateTime>(R"(SELECT "ts" FROM "Stamps")");
    REQUIRE(fetched.has_value());
    if (fetched.has_value())
    {
        CHECK(fetched->sqlValue.year == before.sqlValue.year);
        CHECK(fetched->sqlValue.month == before.sqlValue.month);
        CHECK(fetched->sqlValue.day == before.sqlValue.day);
    }
}

// ================================================================================================
// std::optional<SqlDateTime> handles NULL ↔ value transitions
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture, "std::optional<SqlDateTime>: NULL and value round-trip", "[SqlDateTime]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("Stamps")
            .RequiredColumn("id", SqlColumnTypeDefinitions::Integer {})
            .Column("ts", SqlColumnTypeDefinitions::DateTime {});
    });

    auto const dt =
        SqlDateTime { std::chrono::year { 2026 },    std::chrono::month { 5 }, std::chrono::day { 6 }, 9h, 0min, 0s,
                      std::chrono::nanoseconds { 0 } };
    stmt.Prepare(R"(INSERT INTO "Stamps" ("id", "ts") VALUES (?, ?))");
    (void) stmt.Execute(1, std::optional<SqlDateTime> { dt });
    (void) stmt.Execute(2, std::optional<SqlDateTime> {});

    {
        auto const fetched =
            stmt.ExecuteDirectScalar<std::optional<SqlDateTime>>(R"(SELECT "ts" FROM "Stamps" WHERE "id" = 1)");
        REQUIRE(fetched.has_value());
        if (fetched.has_value())
        {
            REQUIRE(fetched->has_value());
            if (fetched->has_value())
                CHECK((*fetched)->sqlValue.year == 2026);
        }
    }

    {
        auto const fetched =
            stmt.ExecuteDirectScalar<std::optional<SqlDateTime>>(R"(SELECT "ts" FROM "Stamps" WHERE "id" = 2)");
        // Either the outer optional is empty (NULL surfaced as nullopt), or the inner is empty.
        if (fetched.has_value())
            CHECK_FALSE(fetched->has_value());
    }
}
