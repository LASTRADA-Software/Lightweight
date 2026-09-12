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

#include <algorithm>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    // Deliberately not noexcept: _locationFilter is a std::string built from a string_view, so
    // construction allocates and can throw. Declaring it noexcept would turn a bad_alloc into a
    // std::terminate.
    explicit ScriptedFaultSource(SqlErrorInfo error,
                                 int failureCount,
                                 std::string_view locationFilter = "SqlFaultSeamTests"):
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

/// Catches every `OnWarning` made while it is the active logger, restoring the previous logger on
/// scope exit. `RetryStalePreparedStatement` announces a re-prepare through exactly one warning, so
/// this is how a test observes that the recovery arm really ran rather than merely that the gate
/// was passed.
class CapturingWarningLogger: public SqlLogger::Null
{
  public:
    CapturingWarningLogger():
        _previous { &SqlLogger::GetLogger() }
    {
        SqlLogger::SetLogger(*this);
    }

    ~CapturingWarningLogger() override
    {
        SqlLogger::SetLogger(*_previous);
    }

    CapturingWarningLogger(CapturingWarningLogger const&) = delete;
    CapturingWarningLogger(CapturingWarningLogger&&) = delete;
    CapturingWarningLogger& operator=(CapturingWarningLogger const&) = delete;
    CapturingWarningLogger& operator=(CapturingWarningLogger&&) = delete;

    void OnWarning(std::string_view const& message) override
    {
        _warnings.emplace_back(message);
    }

    void OnError(SqlErrorInfo const& errorInfo, std::source_location /*sourceLocation*/) override
    {
        _errors.emplace_back(errorInfo);
    }

    /// @param needle Substring to look for.
    /// @return Whether any captured warning contains @p needle.
    [[nodiscard]] bool AnyWarningContains(std::string_view needle) const
    {
        return std::ranges::any_of(_warnings, [needle](std::string const& w) { return w.contains(needle); });
    }

    /// @param sqlState The five-character SQLSTATE to look for.
    /// @return Whether any diagnostic reported through @c OnError carries @p sqlState.
    [[nodiscard]] bool AnyErrorHasState(std::string_view sqlState) const
    {
        return std::ranges::any_of(
            _errors, [sqlState](SqlErrorInfo const& e) { return std::string_view { e.sqlState } == sqlState; });
    }

  private:
    SqlLogger* _previous;
    std::vector<std::string> _warnings;
    std::vector<SqlErrorInfo> _errors;
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
// detail::CheckOdbcCall / detail::CheckOdbcConnectionCall instead of checking the return
// code directly. Both were previously invisible to SqlFaultSource entirely.
// ================================================================================================

/// Creates a fresh probe table for the stale-plan retry tests and returns a parameterless INSERT
/// against it.
///
/// A statement that yields no result set is deliberate. The seam turns a *successful* SQLExecute
/// into a failure verdict, so the retry runs on a handle the driver still considers busy: with a
/// SELECT, the first execute leaves its cursor open and the re-execute fails with
/// 24000 Invalid cursor state on MS SQL Server and PostgreSQL. That situation cannot arise in
/// production - this arm is only ever reached after an execute that genuinely failed, and a failed
/// execute opens no cursor - so the test avoids it rather than the library defending against it.
///
/// @param stmt Statement to create the table through.
/// @param tableName Probe table to create.
/// @return The INSERT statement text, to be prepared byte-identically on every round.
std::string CreateRetryProbeTable(SqlStatement& stmt, std::string_view tableName)
{
    stmt.MigrateDirect([tableName](SqlMigrationQueryBuilder& migration) {
        migration.CreateTable(std::string(tableName))
            .PrimaryKeyWithAutoIncrement("id")
            .RequiredColumn("value", SqlColumnTypeDefinitions::Integer {});
    });
    return stmt.Query(tableName).Insert().Set("value", 42).ToSql();
}

/// @param tableName Table to count.
/// @return The number of rows currently in @p tableName.
///
/// Deliberately counts through a statement of its own. Reusing the statement under test would
/// prepare a different query on that handle, which clears `m_reusedPreparedQuery` - the very
/// precondition `RetryStalePreparedStatement` gates on - and silently disarm the test.
[[nodiscard]] int RowCount(std::string_view tableName)
{
    auto stmt = SqlStatement {};
    return stmt.ExecuteDirectScalar<int>(stmt.Query(tableName).Select().Count()).value_or(-1);
}

/// Prepares and executes @p query once, so that a later `Prepare` of the same text reuses the
/// handle's already-prepared statement and sets `m_reusedPreparedQuery` - the precondition
/// `RetryStalePreparedStatement` needs before an installed fault source can matter at all.
///
/// @param stmt Statement to warm up.
/// @param query Query text, reused byte-identically on the next round.
void WarmUpReusedPreparedQuery(SqlStatement& stmt, std::string_view query)
{
    stmt.Prepare(query);
    (void) stmt.Execute();
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlFaultSource drives RetryStalePreparedStatement through an actual re-prepare",
                 "[SqlFaultSeam]")
{
    // The point of routing this gate through the seam is not that the seam gets consulted - it is
    // that the recovery arm behind the gate becomes reachable. That needs the injected diagnostic
    // to survive: the real SQLExecute succeeded, so SqlErrorInfo::FromStatementHandle would report
    // an empty SQLSTATE, match no stale-plan state, and return before re-preparing.
    auto stmt = SqlStatement {};
    auto const query = CreateRetryProbeTable(stmt, "FaultSeamRetry");
    WarmUpReusedPreparedQuery(stmt, query);

    // 42S02 is one of the SQLSTATEs the function treats as "the cached plan is stale", so this is
    // the diagnostic that must reach the state comparison for the re-prepare to happen.
    ScriptedFaultSource source { MakeError("42S02", "cached plan is stale"), 1, "RetryStalePreparedStatement" };
    ScopedFaultSource const installed { &source };
    CapturingWarningLogger const warnings;

    stmt.Prepare(query);
    (void) stmt.Execute();

    // The re-prepare announces itself with exactly one warning, so this is the assertion that the
    // recovery arm ran rather than the gate merely being passed. It fails if the injected error is
    // dropped on the way out of the seam: the handle would report an empty SQLSTATE, match no
    // stale-plan state, and return before re-preparing.
    CHECK(warnings.AnyWarningContains("Re-preparing statement"));
    CHECK(source.ConsultCount() >= 1);

    // Three rows, and every one of them is evidence: the warm-up wrote the first, the execute that
    // the seam then reported as failed really did write the second, and the third exists only
    // because the recovery arm re-prepared and the caller re-executed. A retry that did not happen
    // leaves two.
    CHECK(RowCount("FaultSeamRetry") == 3);
}

TEST_CASE_METHOD(SqlTestFixture,
                 "SqlFaultSource's injected non-stale diagnostic leaves the prepared statement alone",
                 "[SqlFaultSeam]")
{
    // The complement of the test above, and the reason the injected diagnostic has to be the real
    // one rather than a stand-in for "something failed": only the stale-plan SQLSTATEs earn a
    // re-prepare, and 08S01 is not one of them.
    auto stmt = SqlStatement {};
    auto const query = CreateRetryProbeTable(stmt, "FaultSeamNoRetry");
    WarmUpReusedPreparedQuery(stmt, query);

    ScriptedFaultSource source { MakeError("08S01", "connection dropped"), 1, "RetryStalePreparedStatement" };
    ScopedFaultSource const installed { &source };
    CapturingWarningLogger const warnings;

    stmt.Prepare(query);
    (void) stmt.Execute();

    CHECK(source.ConsultCount() >= 1);
    CHECK_FALSE(warnings.AnyWarningContains("Re-preparing statement"));
    // Two rows rather than three: the gate was passed, but 08S01 is not a stale-plan state, so no
    // re-prepare and no second execute.
    CHECK(RowCount("FaultSeamNoRetry") == 2);
}

TEST_CASE_METHOD(SqlTestFixture, "SqlFaultSource forces the SQL_COPT_SS_ENCRYPT connection check to fail", "[SqlFaultSeam]")
{
    // SqlConnection::Connect(SqlConnectionDataSource const&) checks the pre-connect encryption
    // attribute via detail::CheckOdbcConnectionCall(sqlReturn, m_hDbc) instead of a bare
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
    CapturingWarningLogger const diagnostics;
    CHECK_FALSE(connection.Connect(dataSource));
    CHECK(source.ConsultCount() >= 1);

    // And the branch reports the diagnostic it was actually given. The SQLSetConnectAttrW that
    // preceded it really succeeded, so LastError() has nothing to do with this failure: reading the
    // handle instead of the outcome would log an empty or unrelated driver diagnostic here.
    CHECK(diagnostics.AnyErrorHasState("HYC00"));
}

// ================================================================================================
// detail::CheckOdbcCall / detail::CheckOdbcConnectionCall: direct coverage of the helpers'
// own safety guards, exercised without going through a real recovery call site.
// ================================================================================================

TEST_CASE("detail::CheckOdbcCall is never consulted for a null statement handle", "[SqlFaultSeam]")
{
    // Mirrors "SqlFaultSource is never consulted for a null statement handle" above, but calls the
    // helper directly rather than through RequireSuccess.
    ScriptedFaultSource source { MakeError("08S01"), 1000 };
    ScopedFaultSource const installed { &source };

    CHECK(detail::CheckOdbcCall(SQL_SUCCESS, SQL_NULL_HSTMT));
    CHECK(source.ConsultCount() == 0);
}

TEST_CASE("detail::CheckOdbcConnectionCall is never consulted for a null connection handle", "[SqlFaultSeam]")
{
    ScriptedFaultSource source { MakeError("HYC00"), 1000 };
    ScopedFaultSource const installed { &source };

    CHECK(detail::CheckOdbcConnectionCall(SQL_SUCCESS, SQL_NULL_HDBC));
    CHECK(source.ConsultCount() == 0);
}

TEST_CASE("detail::CheckOdbcConnectionCall returns false for a genuine failure without consulting the seam",
          "[SqlFaultSeam]")
{
    // Injection only ever turns a success into a failure, never the reverse: a call that actually
    // failed short-circuits before the fault source is asked at all. SQL_NULL_HDBC is fine here -
    // this branch returns before the handle is ever inspected.
    ScriptedFaultSource source { MakeError("HYC00"), 1000 };
    ScopedFaultSource const installed { &source };

    CHECK_FALSE(detail::CheckOdbcConnectionCall(SQL_ERROR, SQL_NULL_HDBC));
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

    CHECK(detail::CheckOdbcConnectionCall(SQL_SUCCESS, hDbc));
}
