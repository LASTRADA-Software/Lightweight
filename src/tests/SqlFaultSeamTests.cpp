// SPDX-License-Identifier: Apache-2.0
//
// Tests for the fault-injection seam.
//
// Some error-recovery paths cannot be reached through a real driver. The backup/restore workers
// retry on *transient* errors (SQLSTATE class 08, HYT00/HYT01, class 40), but every fault a test
// can provoke from the outside — an unreachable driver, an unwritable database path, a dropped
// table — surfaces as HY000, which the retry policy classifies as non-transient. Those arms are
// therefore unreachable, not merely untested, without a seam.
//
// SqlFaultSource closes that gap: it substitutes a scripted failure for a call the driver reported
// as successful, so a test can make any SQLSTATE appear at the point the library checks a
// statement handle.

#include "Utils.hpp"

#include <Lightweight/SqlBackup/Common.hpp>
#include <Lightweight/SqlError.hpp>
#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/Utils.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

using namespace Lightweight;

namespace
{

/// Fails the first @c failureCount checks with a scripted diagnostic, then lets calls through.
/// Mirrors how a transient outage behaves: a burst of failures followed by recovery.
class ScriptedFaultSource: public SqlFaultSource
{
  public:
    ScriptedFaultSource(SqlErrorInfo error, int failureCount) noexcept:
        _error { std::move(error) },
        _remaining { failureCount }
    {
    }

    [[nodiscard]] std::optional<SqlErrorInfo> NextFailure(SQLHSTMT /*hStmt*/,
                                                          std::source_location const& sourceLocation) override
    {
        // Only fail an execution this test file asked for, so an injected fault cannot disturb
        // unrelated library work happening on the same connection.
        //
        // SqlStatement::ExecuteDirect defaults its `location` parameter to the *caller's*
        // source_location and forwards it, so an execution requested from here reports this file;
        // the library's own internal checks (SQLAllocHandle, SQLFreeStmt) report SqlStatement.cpp.
        //
        // This narrowing is a convenience, not a safety requirement: RequireSuccess itself skips
        // injection while the handle is still SQL_NULL_HSTMT, so the half-constructed-statement
        // hazard is closed in the library rather than by this filter.
        auto const file = std::string_view { sourceLocation.file_name() };
        if (!file.contains("SqlFaultSeamTests"))
            return std::nullopt;

        ++_consultCount;
        if (_remaining <= 0)
            return std::nullopt;

        --_remaining;
        return _error;
    }

    [[nodiscard]] int ConsultCount() const noexcept
    {
        return _consultCount;
    }

  private:
    SqlErrorInfo _error;
    int _remaining;
    int _consultCount = 0;
};

/// Installs a fault source for the duration of a scope and clears it again, so a failing
/// assertion cannot leak the override into later tests.
class ScopedFaultSource
{
  public:
    explicit ScopedFaultSource(SqlFaultSource* source) noexcept
    {
        SetFaultSource(source);
    }

    ScopedFaultSource(ScopedFaultSource const&) = delete;
    ScopedFaultSource& operator=(ScopedFaultSource const&) = delete;
    ScopedFaultSource(ScopedFaultSource&&) = delete;
    ScopedFaultSource& operator=(ScopedFaultSource&&) = delete;

    ~ScopedFaultSource()
    {
        SetFaultSource(nullptr);
    }
};

SqlErrorInfo MakeError(std::string sqlState, std::string message = {})
{
    return SqlErrorInfo {
        .nativeErrorCode = 0,
        .sqlState = std::move(sqlState),
        .message = std::move(message),
    };
}

} // namespace

TEST_CASE("SqlFaultSource install and clear round-trips", "[SqlFaultSeam]")
{
    // Asserted as a round-trip rather than as "null before installing": the slot is process-global,
    // so a bare precondition check would depend on test ordering (the suite is run with
    // `--order rand` in some setups) and on no other translation unit leaking a source.
    auto* const before = GetFaultSource();

    ScriptedFaultSource source { MakeError("08S01"), 0 };
    {
        ScopedFaultSource const installed { &source };
        CHECK(GetFaultSource() == &source);
    }
    CHECK(GetFaultSource() == before);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFaultSource is never consulted for a null statement handle", "[SqlFaultSeam]")
{
    // RequireSuccess guards SQLAllocHandle during SqlStatement construction, where the handle is
    // not yet valid. Injecting there would unwind out of the constructor with the handle allocated
    // but unowned, leaking it. The library skips injection while hStmt is SQL_NULL_HSTMT, so
    // constructing a statement under an always-failing source must still succeed.
    ScriptedFaultSource source { MakeError("08S01"), 1000 };
    ScopedFaultSource const installed { &source };

    CHECK_NOTHROW(SqlStatement {});

    // Directly, too: a null handle is skipped before the source is ever asked.
    CHECK_NOTHROW(RequireSuccess(SQL_NULL_HSTMT, SQL_SUCCESS));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFaultSource turns a successful statement into a SqlException", "[SqlFaultSeam]")
{
    ScriptedFaultSource source { MakeError("08S01", "connection dropped"), 1 };
    ScopedFaultSource const installed { &source };

    auto stmt = SqlStatement {};

    // The statement itself is valid — the driver succeeds and the seam substitutes the failure.
    CHECK_THROWS_AS(stmt.ExecuteDirect("SELECT 1"), SqlException);
    CHECK(source.ConsultCount() >= 1);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFaultSource injects a SQLSTATE the retry policy treats as transient", "[SqlFaultSeam]")
{
    // This is the point of the seam: HY000 is what every externally-provokable fault reports, and
    // it is NOT transient. Only an injected class-08 error reaches the retry arms.
    CHECK_FALSE(SqlBackup::detail::IsTransientError(MakeError("HY000", "no such table")));

    ScriptedFaultSource source { MakeError("08S01", "connection dropped"), 1 };
    ScopedFaultSource const installed { &source };

    auto stmt = SqlStatement {};
    try
    {
        (void) stmt.ExecuteDirect("SELECT 1");
        FAIL("expected the injected failure to surface");
    }
    catch (SqlException const& e)
    {
        CHECK(e.info().sqlState == "08S01");
        CHECK(SqlBackup::detail::IsTransientError(e.info()));
    }
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFaultSource stops failing once its script is exhausted", "[SqlFaultSeam]")
{
    ScriptedFaultSource source { MakeError("08S01"), 1 };
    ScopedFaultSource const installed { &source };

    {
        auto stmt = SqlStatement {};
        CHECK_THROWS_AS(stmt.ExecuteDirect("SELECT 1"), SqlException);
    }

    // The script is spent, so the real (successful) result is allowed through. This is what lets a
    // test drive "fails N times, then recovers" retry behaviour.
    //
    // A *fresh* statement is used deliberately. Injecting a failure after the driver already
    // executed the query leaves that statement's cursor open, so reusing it raises
    // "HY010 - The cursor is open" on MS SQL Server and PostgreSQL (SQLite is more permissive).
    // The production retry paths behave the same way: they discard the connection and reconnect
    // rather than re-executing on the statement that failed.
    auto recovered = SqlStatement {};
    CHECK_NOTHROW(recovered.ExecuteDirect("SELECT 1"));
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFaultSource costs nothing when no source is installed", "[SqlFaultSeam]")
{
    REQUIRE(GetFaultSource() == nullptr);

    // With no source configured the success path is the plain early return it always was.
    auto stmt = SqlStatement {};
    CHECK_NOTHROW(stmt.ExecuteDirect("SELECT 1"));
}
