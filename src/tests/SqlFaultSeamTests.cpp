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
///
/// Narrows which call site it reacts to via a substring match against the reported
/// @c std::source_location, so an injected fault cannot disturb unrelated library work happening on
/// the same connection. The default, "SqlFaultSeamTests", matches an execution requested from this
/// file (SqlStatement::ExecuteDirect defaults its `location` parameter to the caller's
/// source_location and forwards it); a call passed explicitly narrows to one specific internal call
/// site instead (e.g. "RetryStalePreparedStatement" or "Connect"), which is needed for checks the
/// library performs internally and reports its own file/function for.
///
/// This narrowing is a convenience, not a safety requirement: the seam itself skips injection while
/// the handle is still null, so the half-constructed/half-destroyed-object hazard is closed in the
/// library rather than by this filter.
class ScriptedFaultSource: public SqlFaultSource
{
  public:
    explicit ScriptedFaultSource(SqlErrorInfo error,
                                 int failureCount,
                                 std::string_view locationFilter = "SqlFaultSeamTests") noexcept:
        _error { std::move(error) },
        _remaining { failureCount },
        _locationFilter { locationFilter }
    {
    }

    [[nodiscard]] std::optional<SqlErrorInfo> NextFailure(SQLHSTMT /*hStmt*/,
                                                          std::source_location const& sourceLocation) override
    {
        return Consult(sourceLocation);
    }

    [[nodiscard]] std::optional<SqlErrorInfo> NextConnectionFailure(SQLHDBC /*hDbc*/,
                                                                    std::source_location const& sourceLocation) override
    {
        return Consult(sourceLocation);
    }

    [[nodiscard]] int ConsultCount() const noexcept
    {
        return _consultCount;
    }

  private:
    [[nodiscard]] std::optional<SqlErrorInfo> Consult(std::source_location const& sourceLocation)
    {
        auto const file = std::string_view { sourceLocation.file_name() };
        auto const function = std::string_view { sourceLocation.function_name() };
        if (!file.contains(_locationFilter) && !function.contains(_locationFilter))
            return std::nullopt;

        ++_consultCount;
        if (_remaining <= 0)
            return std::nullopt;

        --_remaining;
        return _error;
    }

    SqlErrorInfo _error;
    int _remaining;
    int _consultCount = 0;
    std::string _locationFilter;
};

/// A minimal fake predating connection-handle support (#585): overrides only @c NextFailure, the
/// only extension point that existed before. Used to prove @c SqlFaultSource::NextConnectionFailure
/// keeps its documented default (never injects) for such a fake, so it keeps compiling and
/// behaving exactly as before.
class StatementOnlyFaultSource: public SqlFaultSource
{
  public:
    explicit StatementOnlyFaultSource(SqlErrorInfo error) noexcept:
        _error { std::move(error) }
    {
    }

    [[nodiscard]] std::optional<SqlErrorInfo> NextFailure(SQLHSTMT /*hStmt*/,
                                                          std::source_location const& /*sourceLocation*/) override
    {
        return _error;
    }

  private:
    SqlErrorInfo _error;
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

// ================================================================================================
// Retrofitted call sites (#585): inline SQL_SUCCEEDED(...) checks that now go through the seam via
// detail::OdbcCallSucceeded / detail::OdbcConnectionCallSucceeded instead of checking the return
// code directly. Both were previously invisible to SqlFaultSource entirely.
// ================================================================================================

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlFaultSource forces RetryStalePreparedStatement past its initial success gate",
                 "[SqlFaultSeam]")
{
    // RetryStalePreparedStatement's initial gate now reads
    // `detail::OdbcCallSucceeded(result, m_hStmt) || result == SQL_NO_DATA || !m_reusedPreparedQuery`
    // instead of a bare `SQL_SUCCEEDED(result) || ...`. Before that retrofit, GetFaultSource() was
    // never even consulted here: the early return happened first, unconditionally, on a real success.
    auto stmt = SqlStatement {};
    auto const* const query = "SELECT 1";

    // First round: not a reused prepare (m_reusedPreparedQuery is still false), so this round must
    // not consult the seam at all - the fault source is only installed afterwards.
    stmt.Prepare(query);
    {
        auto cursor = stmt.Execute();
        CHECK(cursor.FetchRow());
    }

    // Second round: byte-identical query text, so Prepare() reuses the handle's prepared statement
    // and m_reusedPreparedQuery becomes true - the precondition the gate needs before an installed
    // fault source can matter.
    ScriptedFaultSource source { MakeError("08S01", "connection dropped"), 1, "RetryStalePreparedStatement" };
    ScopedFaultSource const installed { &source };

    stmt.Prepare(query);
    auto cursor = stmt.Execute();

    // The real SQLExecute succeeded and the fault source's diagnostic never lands on the handle
    // (only the seam's boolean verdict is faked, not SqlErrorInfo::FromStatementHandle), so the
    // SQLSTATE the function inspects is not one of the stale-plan states and no re-prepare happens.
    // The observable behaviour is therefore unchanged - the row still comes back...
    CHECK(cursor.FetchRow());
    // ...but the seam was reached and asked, which is only possible once the gate stops
    // short-circuiting on the bare return code. This is the assertion that fails if the retrofit is
    // reverted: ConsultCount stays 0 when the gate goes back to a bare SQL_SUCCEEDED(result).
    CHECK(source.ConsultCount() >= 1);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFaultSource forces the SQL_COPT_SS_ENCRYPT connection check to fail", "[SqlFaultSeam]")
{
    // SqlConnection::Connect(SqlConnectionDataSource const&) checks the pre-connect encryption
    // attribute via detail::OdbcConnectionCallSucceeded(sqlReturn, m_hDbc) instead of a bare
    // SQL_SUCCEEDED(sqlReturn). No driver in the test matrix rejects SQL_COPT_SS_ENCRYPT at set
    // time (see the comment at the call site), so a fault source is the only way to exercise the
    // "fail the connection rather than silently downgrade" branch at all.
    ScriptedFaultSource source { MakeError("HYC00", "encryption attribute rejected"), 1, "Connect" };
    ScopedFaultSource const installed { &source };

    // The datasource name never has to resolve to anything real: the injected failure fires before
    // SQLConnectW is ever reached, at the attribute-set step that precedes it.
    auto connection = SqlConnection { std::nullopt };
    auto const dataSource = SqlConnectionDataSource {
        .datasource = "lightweight-585-does-not-exist",
        .username = {},
        .password = {},
        .encryption = SqlEncryptionMode::Enabled,
    };

    // This fails if the retrofit is reverted for a different reason than a bogus DSN would: with a
    // bare SQL_SUCCEEDED(sqlReturn), Connect() still returns false eventually (SQLConnectW rejects
    // the unresolvable DSN), but the seam is never consulted - ConsultCount stays 0. With the
    // retrofit, the injected failure fires first, before SQLConnectW is ever called.
    CHECK_FALSE(connection.Connect(dataSource));
    CHECK(source.ConsultCount() >= 1);
}

// ================================================================================================
// detail::OdbcCallSucceeded / detail::OdbcConnectionCallSucceeded: direct coverage of the helpers'
// own safety guards, exercised without going through a real recovery call site.
// ================================================================================================

TEST_CASE("detail::OdbcCallSucceeded is never consulted for a null statement handle", "[SqlFaultSeam]")
{
    // Mirrors "SqlFaultSource is never consulted for a null statement handle" above, but calls the
    // helper directly rather than through RequireSuccess.
    ScriptedFaultSource source { MakeError("08S01"), 1000 };
    ScopedFaultSource const installed { &source };

    CHECK(detail::OdbcCallSucceeded(SQL_SUCCESS, SQL_NULL_HSTMT));
    CHECK(source.ConsultCount() == 0);
}

TEST_CASE("detail::OdbcConnectionCallSucceeded is never consulted for a null connection handle", "[SqlFaultSeam]")
{
    ScriptedFaultSource source { MakeError("HYC00"), 1000 };
    ScopedFaultSource const installed { &source };

    CHECK(detail::OdbcConnectionCallSucceeded(SQL_SUCCESS, SQL_NULL_HDBC));
    CHECK(source.ConsultCount() == 0);
}

TEST_CASE("detail::OdbcConnectionCallSucceeded returns false for a genuine failure without consulting the seam",
          "[SqlFaultSeam]")
{
    // Injection only ever turns a success into a failure, never the reverse: a call that actually
    // failed short-circuits before the fault source is asked at all. SQL_NULL_HDBC is fine here -
    // this branch returns before the handle is ever inspected.
    ScriptedFaultSource source { MakeError("HYC00"), 1000 };
    ScopedFaultSource const installed { &source };

    CHECK_FALSE(detail::OdbcConnectionCallSucceeded(SQL_ERROR, SQL_NULL_HDBC));
    CHECK(source.ConsultCount() == 0);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlFaultSource's default NextConnectionFailure never injects, for fakes that predate "
                 "connection-handle support",
                 "[SqlFaultSeam]")
{
    // A live (if unrelated) connection handle, obtained before the fault source is installed below -
    // StatementOnlyFaultSource::NextFailure injects unconditionally, so constructing this under it
    // would fail on the statement's own setup calls, which is not what this test is about.
    auto stmt = SqlStatement {};
    auto* const hDbc = stmt.Connection().NativeHandle();

    // A fake that overrides only NextFailure (the pre-#585 seam surface) must leave connection
    // checks completely unaffected: SqlFaultSource::NextConnectionFailure's default body always
    // returns nullopt, regardless of what NextFailure would have injected.
    StatementOnlyFaultSource source { MakeError("HYC00", "should never surface on a connection check") };
    ScopedFaultSource const installed { &source };

    CHECK(detail::OdbcConnectionCallSucceeded(SQL_SUCCESS, hDbc));
}
