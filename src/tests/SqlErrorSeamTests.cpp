// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the error-handling seam: the classification policy and the injectable diagnostic
// source. Neither needs a database, so these run identically on every platform and DBMS.
//
// Before this seam existed, the policy below was unreachable from a test: deciding how a failed
// ODBC call surfaces required a driver that returned a specific SQLSTATE on demand.

#include <Lightweight/SqlError.hpp>
#include <Lightweight/Utils.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <utility>

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

/// A diagnostic source that reports one scripted error for every handle.
class ScriptedDiagnosticSource: public SqlDiagnosticSource
{
  public:
    explicit ScriptedDiagnosticSource(SqlErrorInfo error) noexcept:
        _error { std::move(error) }
    {
    }

    [[nodiscard]] SqlErrorInfo Diagnose(SQLSMALLINT /*handleType*/, SQLHANDLE /*handle*/) override
    {
        ++_callCount;
        return _error;
    }

    [[nodiscard]] int CallCount() const noexcept
    {
        return _callCount;
    }

  private:
    SqlErrorInfo _error;
    int _callCount = 0;
};

/// Installs a diagnostic source for the duration of a scope and clears it again, so a failing
/// assertion cannot leak the override into later tests.
class ScopedDiagnosticSource
{
  public:
    explicit ScopedDiagnosticSource(SqlDiagnosticSource& source) noexcept
    {
        SetDiagnosticSource(&source);
    }

    ScopedDiagnosticSource(ScopedDiagnosticSource const&) = delete;
    ScopedDiagnosticSource& operator=(ScopedDiagnosticSource const&) = delete;
    ScopedDiagnosticSource(ScopedDiagnosticSource&&) = delete;
    ScopedDiagnosticSource& operator=(ScopedDiagnosticSource&&) = delete;

    ~ScopedDiagnosticSource()
    {
        SetDiagnosticSource(nullptr);
    }
};

} // namespace

// ================================================================================================
// ClassifyOdbcResult - the error-handling policy, in isolation
// ================================================================================================

TEST_CASE("ClassifyOdbcResult: success codes require no action", "[SqlError][classification]")
{
    // Both ODBC success codes must classify as None regardless of what the diagnostics say -
    // SQL_SUCCESS_WITH_INFO carries diagnostics but is not a failure.
    CHECK(ClassifyOdbcResult(SQL_SUCCESS, MakeError("00000")) == SqlFailureAction::None);
    CHECK(ClassifyOdbcResult(SQL_SUCCESS_WITH_INFO, MakeError("01004")) == SqlFailureAction::None);

    // Even a would-be soft-failure SQLSTATE must not trigger on a success code.
    CHECK(ClassifyOdbcResult(SQL_SUCCESS, MakeError("07009")) == SqlFailureAction::None);
}

TEST_CASE("ClassifyOdbcResult: 07009 is demoted to invalid_argument", "[SqlError][classification]")
{
    // Invalid Descriptor Index. Some drivers omit optional columns, and callers recover by
    // substituting a default rather than failing the query - so this must not become SqlException.
    CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("07009")) == SqlFailureAction::ThrowInvalidArgument);
}

TEST_CASE("ClassifyOdbcResult: other failures become SqlException", "[SqlError][classification]")
{
    CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("23000")) == SqlFailureAction::ThrowSqlException); // constraint
    CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("42S02")) == SqlFailureAction::ThrowSqlException); // no such table
    CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("08S01")) == SqlFailureAction::ThrowSqlException); // link failure
    CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("HYT00")) == SqlFailureAction::ThrowSqlException); // timeout
    CHECK(ClassifyOdbcResult(SQL_INVALID_HANDLE, MakeError("HY000")) == SqlFailureAction::ThrowSqlException);

    // An empty/unknown SQLSTATE must still be treated as a genuine error, never silently ignored.
    CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("")) == SqlFailureAction::ThrowSqlException);
}

TEST_CASE("ClassifyOdbcResult: only the exact 07009 state is demoted", "[SqlError][classification]")
{
    // Guards against a prefix/substring comparison creeping in: neighbouring 070xx states are
    // genuine errors and must not inherit the demotion.
    CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("07001")) == SqlFailureAction::ThrowSqlException);
    CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("07006")) == SqlFailureAction::ThrowSqlException);
    CHECK(ClassifyOdbcResult(SQL_ERROR, MakeError("0700")) == SqlFailureAction::ThrowSqlException);
}

// ================================================================================================
// SqlDiagnosticSource - driving the propagation path without a database
// ================================================================================================

TEST_CASE("SqlDiagnosticSource: absent by default", "[SqlError][seam]")
{
    // Production must not carry an installed source; the real ODBC reader has to be the default.
    CHECK(GetDiagnosticSource() == nullptr);
}

TEST_CASE("SqlDiagnosticSource: install and clear", "[SqlError][seam]")
{
    auto source = ScriptedDiagnosticSource { MakeError("23000") };

    {
        auto const installed = ScopedDiagnosticSource { source };
        CHECK(GetDiagnosticSource() == &source);
    }

    CHECK(GetDiagnosticSource() == nullptr);
}

TEST_CASE("RequireSuccess: throws SqlException carrying the scripted diagnostics", "[SqlError][seam]")
{
    auto source = ScriptedDiagnosticSource { MakeError("23000", 2627, "Violation of UNIQUE KEY constraint") };
    auto const installed = ScopedDiagnosticSource { source };

    // A null handle is fine: the scripted source never dereferences it, which is precisely what
    // makes this path reachable without a live statement.
    try
    {
        RequireSuccess(nullptr, SQL_ERROR);
        FAIL("RequireSuccess must throw on SQL_ERROR");
    }
    catch (SqlException const& e)
    {
        CHECK(e.info().sqlState == "23000");
        CHECK(e.info().nativeErrorCode == 2627);
        CHECK(std::string { e.what() }.contains("Violation of UNIQUE KEY constraint"));
    }

    CHECK(source.CallCount() == 1);
}

TEST_CASE("RequireSuccess: 07009 surfaces as invalid_argument, not SqlException", "[SqlError][seam]")
{
    auto source = ScriptedDiagnosticSource { MakeError("07009", 0, "Invalid Descriptor Index") };
    auto const installed = ScopedDiagnosticSource { source };

    // The distinction matters: callers that read optional columns catch std::invalid_argument to
    // substitute a default. If this regressed to SqlException those call sites would start failing
    // queries that previously succeeded.
    CHECK_THROWS_AS(RequireSuccess(nullptr, SQL_ERROR), std::invalid_argument);
}

TEST_CASE("RequireSuccess: success codes never consult the diagnostic source", "[SqlError][seam]")
{
    auto source = ScriptedDiagnosticSource { MakeError("23000") };
    auto const installed = ScopedDiagnosticSource { source };

    CHECK_NOTHROW(RequireSuccess(nullptr, SQL_SUCCESS));
    CHECK_NOTHROW(RequireSuccess(nullptr, SQL_SUCCESS_WITH_INFO));

    // The zero call count is the evidence that the seam adds nothing to the success path: it is
    // reached only after SQL_SUCCEEDED has already returned false.
    CHECK(source.CallCount() == 0);
}

TEST_CASE("SqlErrorInfo::FromStatementHandle: routes through the installed source", "[SqlError][seam]")
{
    auto source = ScriptedDiagnosticSource { MakeError("42S02", 208, "Invalid object name") };
    auto const installed = ScopedDiagnosticSource { source };

    auto const info = SqlErrorInfo::FromStatementHandle(nullptr);

    CHECK(info.sqlState == "42S02");
    CHECK(info.nativeErrorCode == 208);
    CHECK(info.message == "Invalid object name");
    CHECK(source.CallCount() == 1);
}
