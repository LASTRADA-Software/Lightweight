// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlErrorDetection.hpp>
#include <Lightweight/SqlServerType.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>

using namespace Lightweight;

namespace
{

SqlErrorInfo MakeError(std::string sqlState, SQLINTEGER nativeCode = 0, std::string message = {})
{
    return SqlErrorInfo {
        .nativeErrorCode = nativeCode,
        .sqlState = std::move(sqlState),
        .message = std::move(message),
    };
}

} // namespace

// ================================================================================================
// IsTableAlreadyExistsError
// ================================================================================================

TEST_CASE("IsTableAlreadyExistsError MICROSOFT_SQL by SQLSTATE", "[SqlErrorDetection]")
{
    auto const err = MakeError("42S01");
    CHECK(IsTableAlreadyExistsError(err, SqlServerType::MICROSOFT_SQL));
}

TEST_CASE("IsTableAlreadyExistsError MICROSOFT_SQL by native code 2714", "[SqlErrorDetection]")
{
    auto const err = MakeError("HY000", 2714);
    CHECK(IsTableAlreadyExistsError(err, SqlServerType::MICROSOFT_SQL));
}

TEST_CASE("IsTableAlreadyExistsError MICROSOFT_SQL rejects unrelated error", "[SqlErrorDetection]")
{
    auto const err = MakeError("42S02", 208);
    CHECK_FALSE(IsTableAlreadyExistsError(err, SqlServerType::MICROSOFT_SQL));
}

TEST_CASE("IsTableAlreadyExistsError POSTGRESQL by SQLSTATE", "[SqlErrorDetection]")
{
    auto const err = MakeError("42P07");
    CHECK(IsTableAlreadyExistsError(err, SqlServerType::POSTGRESQL));
}

TEST_CASE("IsTableAlreadyExistsError POSTGRESQL rejects MS-only state", "[SqlErrorDetection]")
{
    auto const err = MakeError("42S01");
    CHECK_FALSE(IsTableAlreadyExistsError(err, SqlServerType::POSTGRESQL));
}

TEST_CASE("IsTableAlreadyExistsError SQLITE by message", "[SqlErrorDetection]")
{
    auto const err = MakeError("HY000", 1, "table T already exists");
    CHECK(IsTableAlreadyExistsError(err, SqlServerType::SQLITE));
}

TEST_CASE("IsTableAlreadyExistsError SQLITE rejects unrelated message", "[SqlErrorDetection]")
{
    auto const err = MakeError("HY000", 1, "no such table: T");
    CHECK_FALSE(IsTableAlreadyExistsError(err, SqlServerType::SQLITE));
}

TEST_CASE("IsTableAlreadyExistsError MYSQL by SQLSTATE", "[SqlErrorDetection]")
{
    auto const err = MakeError("42S01");
    CHECK(IsTableAlreadyExistsError(err, SqlServerType::MYSQL));
}

TEST_CASE("IsTableAlreadyExistsError MYSQL by native code 1050", "[SqlErrorDetection]")
{
    auto const err = MakeError("HY000", 1050);
    CHECK(IsTableAlreadyExistsError(err, SqlServerType::MYSQL));
}

TEST_CASE("IsTableAlreadyExistsError unknown server falls back to message", "[SqlErrorDetection]")
{
    auto const ok = MakeError("HY000", 0, "relation already exists");
    auto const bad = MakeError("HY000", 0, "completely unrelated");
    CHECK(IsTableAlreadyExistsError(ok, SqlServerType::UNKNOWN));
    CHECK_FALSE(IsTableAlreadyExistsError(bad, SqlServerType::UNKNOWN));
}

// ================================================================================================
// IsTableNotFoundError
// ================================================================================================

TEST_CASE("IsTableNotFoundError MICROSOFT_SQL by SQLSTATE", "[SqlErrorDetection]")
{
    auto const err = MakeError("42S02");
    CHECK(IsTableNotFoundError(err, SqlServerType::MICROSOFT_SQL));
}

TEST_CASE("IsTableNotFoundError MICROSOFT_SQL by native code 208", "[SqlErrorDetection]")
{
    auto const err = MakeError("HY000", 208);
    CHECK(IsTableNotFoundError(err, SqlServerType::MICROSOFT_SQL));
}

TEST_CASE("IsTableNotFoundError MICROSOFT_SQL rejects unrelated error", "[SqlErrorDetection]")
{
    auto const err = MakeError("42S01", 2714);
    CHECK_FALSE(IsTableNotFoundError(err, SqlServerType::MICROSOFT_SQL));
}

TEST_CASE("IsTableNotFoundError POSTGRESQL by SQLSTATE", "[SqlErrorDetection]")
{
    auto const err = MakeError("42P01");
    CHECK(IsTableNotFoundError(err, SqlServerType::POSTGRESQL));
}

TEST_CASE("IsTableNotFoundError SQLITE by message", "[SqlErrorDetection]")
{
    auto const err = MakeError("HY000", 1, "no such table: Foo");
    CHECK(IsTableNotFoundError(err, SqlServerType::SQLITE));
}

TEST_CASE("IsTableNotFoundError MYSQL by native code 1146", "[SqlErrorDetection]")
{
    auto const err = MakeError("HY000", 1146);
    CHECK(IsTableNotFoundError(err, SqlServerType::MYSQL));
}

TEST_CASE("IsTableNotFoundError MYSQL by SQLSTATE", "[SqlErrorDetection]")
{
    auto const err = MakeError("42S02");
    CHECK(IsTableNotFoundError(err, SqlServerType::MYSQL));
}

TEST_CASE("IsTableNotFoundError unknown server falls back to message", "[SqlErrorDetection]")
{
    CHECK(IsTableNotFoundError(MakeError("HY000", 0, "relation does not exist"), SqlServerType::UNKNOWN));
    CHECK(IsTableNotFoundError(MakeError("HY000", 0, "no such relation"), SqlServerType::UNKNOWN));
    CHECK(IsTableNotFoundError(MakeError("HY000", 0, "table not found"), SqlServerType::UNKNOWN));
    CHECK_FALSE(IsTableNotFoundError(MakeError("HY000", 0, "syntax error"), SqlServerType::UNKNOWN));
}

// ================================================================================================
// IsTransientError
// ================================================================================================

TEST_CASE("IsTransientError SQLSTATE class 08 (connection)", "[SqlErrorDetection]")
{
    CHECK(IsTransientError(MakeError("08001")));
    CHECK(IsTransientError(MakeError("08003")));
    CHECK(IsTransientError(MakeError("08006")));
}

TEST_CASE("IsTransientError SQLSTATE HYT00 / HYT01 (timeout)", "[SqlErrorDetection]")
{
    CHECK(IsTransientError(MakeError("HYT00")));
    CHECK(IsTransientError(MakeError("HYT01")));
}

TEST_CASE("IsTransientError SQLSTATE class 40 (rollback)", "[SqlErrorDetection]")
{
    CHECK(IsTransientError(MakeError("40001")));
    CHECK(IsTransientError(MakeError("40002")));
}

TEST_CASE("IsTransientError SQLite locked / busy via message", "[SqlErrorDetection]")
{
    CHECK(IsTransientError(MakeError("HY000", 1, "database is locked")));
    CHECK(IsTransientError(MakeError("HY000", 1, "got SQLITE_BUSY")));
    CHECK(IsTransientError(MakeError("HY000", 1, "got SQLITE_LOCKED")));
}

TEST_CASE("IsTransientError SQL Server native codes", "[SqlErrorDetection]")
{
    CHECK(IsTransientError(MakeError("HY000", 1205))); // deadlock victim
    CHECK(IsTransientError(MakeError("HY000", 1222))); // lock request timeout
    CHECK(IsTransientError(MakeError("HY000", -2)));   // timeout
}

TEST_CASE("IsTransientError returns false for non-transient errors", "[SqlErrorDetection]")
{
    CHECK_FALSE(IsTransientError(MakeError("42S01")));    // table exists
    CHECK_FALSE(IsTransientError(MakeError("23505")));    // unique violation
    CHECK_FALSE(IsTransientError(MakeError("HY000", 0))); // generic
}

// ================================================================================================
// IsUniqueConstraintViolation
// ================================================================================================

TEST_CASE("IsUniqueConstraintViolation MICROSOFT_SQL native codes", "[SqlErrorDetection]")
{
    CHECK(IsUniqueConstraintViolation(MakeError("23000", 2627), SqlServerType::MICROSOFT_SQL));
    CHECK(IsUniqueConstraintViolation(MakeError("23000", 2601), SqlServerType::MICROSOFT_SQL));
    CHECK_FALSE(IsUniqueConstraintViolation(MakeError("23000", 547), SqlServerType::MICROSOFT_SQL));
}

TEST_CASE("IsUniqueConstraintViolation POSTGRESQL by SQLSTATE", "[SqlErrorDetection]")
{
    CHECK(IsUniqueConstraintViolation(MakeError("23505"), SqlServerType::POSTGRESQL));
    CHECK_FALSE(IsUniqueConstraintViolation(MakeError("23503"), SqlServerType::POSTGRESQL));
}

TEST_CASE("IsUniqueConstraintViolation SQLITE by message", "[SqlErrorDetection]")
{
    CHECK(IsUniqueConstraintViolation(MakeError("HY000", 1, "UNIQUE constraint failed: t.id"), SqlServerType::SQLITE));
    CHECK_FALSE(IsUniqueConstraintViolation(MakeError("HY000", 1, "FOREIGN KEY constraint failed"), SqlServerType::SQLITE));
}

TEST_CASE("IsUniqueConstraintViolation MYSQL native code 1062", "[SqlErrorDetection]")
{
    CHECK(IsUniqueConstraintViolation(MakeError("23000", 1062), SqlServerType::MYSQL));
    CHECK_FALSE(IsUniqueConstraintViolation(MakeError("23000", 1451), SqlServerType::MYSQL));
}

TEST_CASE("IsUniqueConstraintViolation unknown server fallback", "[SqlErrorDetection]")
{
    CHECK(IsUniqueConstraintViolation(MakeError("23000"), SqlServerType::UNKNOWN));
    CHECK(IsUniqueConstraintViolation(MakeError("23505"), SqlServerType::UNKNOWN));
    CHECK_FALSE(IsUniqueConstraintViolation(MakeError("HY000"), SqlServerType::UNKNOWN));
}

// ================================================================================================
// IsForeignKeyViolation
// ================================================================================================

TEST_CASE("IsForeignKeyViolation MICROSOFT_SQL native code 547", "[SqlErrorDetection]")
{
    CHECK(IsForeignKeyViolation(MakeError("23000", 547), SqlServerType::MICROSOFT_SQL));
    CHECK_FALSE(IsForeignKeyViolation(MakeError("23000", 2627), SqlServerType::MICROSOFT_SQL));
}

TEST_CASE("IsForeignKeyViolation POSTGRESQL by SQLSTATE", "[SqlErrorDetection]")
{
    CHECK(IsForeignKeyViolation(MakeError("23503"), SqlServerType::POSTGRESQL));
    CHECK_FALSE(IsForeignKeyViolation(MakeError("23505"), SqlServerType::POSTGRESQL));
}

TEST_CASE("IsForeignKeyViolation SQLITE by message", "[SqlErrorDetection]")
{
    CHECK(IsForeignKeyViolation(MakeError("HY000", 1, "FOREIGN KEY constraint failed"), SqlServerType::SQLITE));
    CHECK_FALSE(IsForeignKeyViolation(MakeError("HY000", 1, "UNIQUE constraint failed"), SqlServerType::SQLITE));
}

TEST_CASE("IsForeignKeyViolation MYSQL native codes 1451 / 1452", "[SqlErrorDetection]")
{
    CHECK(IsForeignKeyViolation(MakeError("23000", 1451), SqlServerType::MYSQL));
    CHECK(IsForeignKeyViolation(MakeError("23000", 1452), SqlServerType::MYSQL));
    CHECK_FALSE(IsForeignKeyViolation(MakeError("23000", 1062), SqlServerType::MYSQL));
}

TEST_CASE("IsForeignKeyViolation unknown server fallback", "[SqlErrorDetection]")
{
    CHECK(IsForeignKeyViolation(MakeError("23503"), SqlServerType::UNKNOWN));
    CHECK_FALSE(IsForeignKeyViolation(MakeError("23000"), SqlServerType::UNKNOWN));
}

// ================================================================================================
// SqlServerType formatter
// ================================================================================================

TEST_CASE("std::formatter<SqlServerType> produces human-readable text", "[SqlServerType]")
{
    CHECK(std::format("{}", SqlServerType::MICROSOFT_SQL) == "Microsoft SQL Server");
    CHECK(std::format("{}", SqlServerType::POSTGRESQL) == "PostgreSQL");
    CHECK(std::format("{}", SqlServerType::SQLITE) == "SQLite");
    CHECK(std::format("{}", SqlServerType::MYSQL) == "MySQL");
    CHECK(std::format("{}", SqlServerType::UNKNOWN) == "Unknown");
}
