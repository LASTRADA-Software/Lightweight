// SPDX-License-Identifier: Apache-2.0

#include "Utils.hpp"

#include <Lightweight/DataBinder/SqlDynamicNumeric.hpp>
#include <Lightweight/DataBinder/SqlGuid.hpp>
#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Lightweight;

namespace
{

/// An integer whose input binder binds a copy of it, and reports that copy to @c observer.
struct CopiedInteger
{
    int value {};
    std::weak_ptr<int const>* observer {};
};

/// An integer whose input binder makes a copy, as @ref CopiedInteger does, but whose bind fails.
struct UnbindableInteger
{
    int value {};
    std::weak_ptr<int const>* observer {};
};

/// A row of the numeric-parameter query, row-wise fetchable (its size is a multiple of SQLLEN's alignment).
struct IdRecord
{
    std::int64_t id {};
};

} // namespace

namespace Lightweight
{

/// Binds a heap copy of the value, as the converting binders do, and hands it to the statement.
template <>
struct SqlDataBinder<CopiedInteger>
{
    static SQLRETURN InputParameter(SQLHSTMT stmt,
                                    SQLUSMALLINT column,
                                    CopiedInteger const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        auto const copy = std::make_shared<int>(value.value);
        *value.observer = copy;
        return detail::KeepAliveIfBound(
            cb,
            column,
            copy,
            SQLBindParameter(stmt, column, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, copy.get(), 0, nullptr));
    }
};

/// Makes a copy of the value, then fails the bind the way a driver rejecting the parameter would.
template <>
struct SqlDataBinder<UnbindableInteger>
{
    static SQLRETURN InputParameter(SQLHSTMT /*stmt*/,
                                    SQLUSMALLINT column,
                                    UnbindableInteger const& value,
                                    SqlDataBinderCallback& cb) noexcept
    {
        auto const copy = std::make_shared<int>(value.value);
        *value.observer = copy;
        return detail::KeepAliveIfBound(cb, column, copy, SQL_ERROR);
    }
};

} // namespace Lightweight

namespace
{

/// Creates the table @p tableName with the integer column "id" and the column "value" of type @p valueType.
void CreateTable(SqlStatement& stmt, std::string_view tableName, SqlColumnTypeDefinition const& valueType)
{
    stmt.MigrateDirect([&](auto& migration) {
        migration.CreateTable(tableName)
            .RequiredColumn("id", SqlColumnTypeDefinitions::Integer {})
            .Column("value", valueType);
    });
}

/// Reads the "value" column of the row with the given @p id of table @p tableName.
template <typename T>
T ReadValue(SqlStatement& stmt, std::string_view tableName, int id)
{
    auto const value =
        stmt.ExecuteDirectScalar<T>(std::format(R"(SELECT "value" FROM "{}" WHERE "id" = {})", tableName, id));
    REQUIRE(value.has_value());
    return value.value_or(T {});
}

} // namespace

TEST_CASE_METHOD(SqlTestFixture, "A binder's copy lives as long as its parameter binding", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateTable(stmt, "CopiedValues", SqlColumnTypeDefinitions::Integer {});
    stmt.Prepare(R"(INSERT INTO "CopiedValues" ("id", "value") VALUES (?, ?))");

    auto const firstId = 1;
    auto firstCopy = std::weak_ptr<int const> {};
    auto const firstValue = CopiedInteger { .value = 7, .observer = &firstCopy };
    stmt.BindInputParameter(1, firstId);
    stmt.BindInputParameter(2, firstValue);
    (void) stmt.Execute();
    CHECK_FALSE(firstCopy.expired());

    // Executing the same binding again reads the copy again.
    auto const secondId = 2;
    stmt.BindInputParameter(1, secondId);
    (void) stmt.Execute();
    CHECK_FALSE(firstCopy.expired());
    CHECK(ReadValue<int>(stmt, "CopiedValues", 1) == 7);
    CHECK(ReadValue<int>(stmt, "CopiedValues", 2) == 7);
    CHECK(firstCopy.expired()); // executing another query directly drops the parameter bindings

    // Binding the parameter anew after an execution releases the previous copy.
    stmt.Prepare(R"(INSERT INTO "CopiedValues" ("id", "value") VALUES (?, ?))");
    auto const thirdId = 3;
    stmt.BindInputParameter(1, thirdId);
    stmt.BindInputParameter(2, firstValue);
    (void) stmt.Execute();
    auto const executedCopy = firstCopy;
    auto secondCopy = std::weak_ptr<int const> {};
    auto const secondValue = CopiedInteger { .value = 8, .observer = &secondCopy };
    stmt.BindInputParameter(2, secondValue);
    CHECK(executedCopy.expired());
    CHECK_FALSE(secondCopy.expired());

    // Preparing another query drops every parameter binding, and with it the copy.
    stmt.Prepare(R"(SELECT COUNT(*) FROM "CopiedValues")");
    CHECK(secondCopy.expired());
}

TEST_CASE_METHOD(SqlTestFixture, "Copies handed over for one parameter before it executes are all kept", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateTable(stmt, "CopiedTwice", SqlColumnTypeDefinitions::Integer {});
    stmt.Prepare(R"(INSERT INTO "CopiedTwice" ("id", "value") VALUES (1, ?))");

    auto firstCopy = std::weak_ptr<int const> {};
    auto secondCopy = std::weak_ptr<int const> {};
    auto const firstValue = CopiedInteger { .value = 1, .observer = &firstCopy };
    auto const secondValue = CopiedInteger { .value = 2, .observer = &secondCopy };
    stmt.BindInputParameter(1, firstValue);
    stmt.BindInputParameter(1, secondValue);
    (void) stmt.Execute();
    CHECK_FALSE(firstCopy.expired());
    CHECK_FALSE(secondCopy.expired());
    CHECK(ReadValue<int>(stmt, "CopiedTwice", 1) == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "A bound GUID can be executed again", "[SqlStatement],[SqlGuid]")
{
    auto stmt = SqlStatement {};
    CreateTable(stmt, "ReboundGuids", SqlColumnTypeDefinitions::Guid {});
    stmt.Prepare(R"(INSERT INTO "ReboundGuids" ("id", "value") VALUES (?, ?))");

    auto const firstId = 1;
    auto const guid = SqlGuid::Create();
    stmt.BindInputParameter(1, firstId);
    stmt.BindInputParameter(2, guid);
    (void) stmt.Execute();
    auto const secondId = 2;
    stmt.BindInputParameter(1, secondId);
    (void) stmt.Execute();

    CHECK(ReadValue<SqlGuid>(stmt, "ReboundGuids", 1) == guid);
    CHECK(ReadValue<SqlGuid>(stmt, "ReboundGuids", 2) == guid);
}

TEST_CASE_METHOD(SqlTestFixture, "A bound UTF-8 string can be executed again", "[SqlStatement],[Unicode]")
{
    auto stmt = SqlStatement {};
    CreateTable(stmt, "ReboundTexts", SqlColumnTypeDefinitions::NVarchar { .size = 40 });
    stmt.Prepare(R"(INSERT INTO "ReboundTexts" ("id", "value") VALUES (?, ?))");

    auto const firstId = 1;
    auto const text = std::u8string { u8"bound once, executed twice" };
    stmt.BindInputParameter(1, firstId);
    stmt.BindInputParameter(2, text);
    (void) stmt.Execute();
    auto const secondId = 2;
    stmt.BindInputParameter(1, secondId);
    (void) stmt.Execute();

    CHECK(ReadValue<std::u8string>(stmt, "ReboundTexts", 1) == text);
    CHECK(ReadValue<std::u8string>(stmt, "ReboundTexts", 2) == text);
}

TEST_CASE_METHOD(SqlTestFixture, "A failed re-bind keeps the previous binding's copy", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateTable(stmt, "FailedRebind", SqlColumnTypeDefinitions::Integer {});
    stmt.Prepare(R"(INSERT INTO "FailedRebind" ("id", "value") VALUES (?, ?))");

    auto const firstId = 1;
    auto boundCopy = std::weak_ptr<int const> {};
    auto const boundValue = CopiedInteger { .value = 7, .observer = &boundCopy };
    stmt.BindInputParameter(1, firstId);
    stmt.BindInputParameter(2, boundValue);
    (void) stmt.Execute();

    // The failed bind leaves the parameter bound to the previous copy, which must therefore survive.
    auto rejectedCopy = std::weak_ptr<int const> {};
    auto const rejectedValue = UnbindableInteger { .value = 8, .observer = &rejectedCopy };
    CHECK_THROWS_AS(stmt.BindInputParameter(2, rejectedValue), SqlException);
    CHECK(rejectedCopy.expired());
    CHECK_FALSE(boundCopy.expired());

    auto const secondId = 2;
    stmt.BindInputParameter(1, secondId);
    (void) stmt.Execute();
    CHECK(ReadValue<int>(stmt, "FailedRebind", 2) == 7);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "A bound numeric can be executed again after a row-wise fetch",
                 "[SqlStatement],[SqlDynamicNumeric]")
{
    auto stmt = SqlStatement {};
    stmt.MigrateDirect([](auto& migration) {
        migration.CreateTable("NumericFilter").RequiredColumn("id", SqlColumnTypeDefinitions::Bigint {});
    });
    stmt.Prepare(R"(INSERT INTO "NumericFilter" ("id") VALUES (?))");
    for (auto const id: { std::int64_t { 1 }, std::int64_t { 2 }, std::int64_t { 3 } })
        (void) stmt.Execute(id);

    // The row-wise fetch releases the statement's scratch buffers; the bound literal must not be one of them.
    stmt.Prepare(R"(SELECT "id" FROM "NumericFilter" WHERE "id" <= ? ORDER BY "id")");
    auto const limit = SqlDynamicNumeric { .unscaledValue = 2, .precision = 10, .scale = 0 };
    stmt.BindInputParameter(1, limit);
    for (auto const execution: { 1, 2 })
    {
        INFO("execution " << execution);
        auto records = std::vector<IdRecord> {};
        auto cursor = stmt.Execute();
        cursor.FetchAllRowWise(records, 4, [](IdRecord& record) -> std::int64_t& { return record.id; });
        REQUIRE(records.size() == 2);
        CHECK(records[0].id == 1);
        CHECK(records[1].id == 2);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "A prepared statement runs a native batch twice", "[SqlStatement]")
{
    auto stmt = SqlStatement {};
    CreateTable(stmt, "BatchTwice", SqlColumnTypeDefinitions::Char { .size = 1 });
    stmt.Prepare(R"(INSERT INTO "BatchTwice" ("id", "value") VALUES (?, ?))");

    // The batch's staged copies are released once it has executed; the next batch binds its own.
    auto const firstIds = std::array { 1, 2 };
    auto const firstLetters = std::array { 'a', 'b' };
    (void) stmt.ExecuteBatchNative(firstIds, firstLetters);
    auto const secondIds = std::array { 3, 4 };
    auto const secondLetters = std::array { 'c', 'd' };
    (void) stmt.ExecuteBatchNative(secondIds, secondLetters);

    CHECK(ReadValue<std::string>(stmt, "BatchTwice", 1) == "a");
    CHECK(ReadValue<std::string>(stmt, "BatchTwice", 2) == "b");
    CHECK(ReadValue<std::string>(stmt, "BatchTwice", 3) == "c");
    CHECK(ReadValue<std::string>(stmt, "BatchTwice", 4) == "d");
}
