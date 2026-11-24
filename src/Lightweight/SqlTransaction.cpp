// SPDX-License-Identifier: Apache-2.0

#include "SqlConnection.hpp"
#include "SqlQueryFormatter.hpp"
#include "SqlStatement.hpp"
#include "SqlTransaction.hpp"
#include "TracyProfiler.hpp"

#include <cstdio>
#include <format>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

namespace Lightweight
{

namespace
{
    /// Checks the result of a transaction call through the connection fault seam, without letting
    /// anything escape: the callers are noexcept, and an installed fault source is user code.
    detail::OdbcCallOutcome CheckTransactionCall(SQLRETURN result,
                                                 SQLHDBC connection,
                                                 std::source_location location) noexcept
    {
        try
        {
            return detail::CheckOdbcConnectionCall(result, connection, location);
        }
        catch (...)
        {
            return detail::OdbcCallOutcome { .succeeded = SQL_SUCCEEDED(result), .injectedError = std::nullopt };
        }
    }

    /// Reads the diagnostic of the transaction call that just failed on @p connection - or takes the one a
    /// fault source injected - and reports it, without letting anything escape: building the diagnostic
    /// allocates and the installed logger is user code free to throw. Read before any other call on the
    /// connection, which would clear it - and the SQLite ODBC driver hands a record out only once.
    /// @return The diagnostic; empty only if it could not be built.
    std::optional<SqlErrorInfo> TakeTransactionError(detail::OdbcCallOutcome const& outcome,
                                                     SQLHDBC connection,
                                                     std::source_location location) noexcept
    {
        auto error = std::optional<SqlErrorInfo> {};
        try
        {
            error = outcome.EffectiveError([connection] { return SqlErrorInfo::FromConnectionHandle(connection); });
            SqlLogger::GetLogger().OnError(*error, location);
        }
        catch (...)
        {
            // The failure itself still reaches the caller through its return value; only the
            // report is lost, so leave a best-effort trace of that.
            std::fputs("SqlTransaction: reading or reporting a transaction error failed.\n", stderr);
        }
        return error;
    }

    /// @return @p what, followed by the driver's diagnostic when there is one.
    std::string FailureMessage(std::string_view what, std::optional<SqlErrorInfo> const& error)
    {
        if (!error)
            return std::string { what };
        return std::format("{}: {}", what, *error);
    }
} // namespace

SqlTransaction::SqlTransaction(SqlConnection& connection,
                               SqlTransactionMode defaultMode,
                               SqlIsolationMode isolationMode,
                               std::source_location location):
    m_connection { &connection },
    m_defaultMode { defaultMode },
    m_location { location }
{
    if (isolationMode != SqlIsolationMode::DriverDefault)
    {
        auto const value = static_cast<SQLULEN>(isolationMode);
#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#endif
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        SQLSetConnectAttr(NativeHandle(), SQL_ATTR_TXN_ISOLATION, (SQLPOINTER) value, SQL_IS_UINTEGER);
#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif
    }

    connection.RequireSuccess(
        SQLSetConnectAttr(NativeHandle(), SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_OFF, SQL_IS_UINTEGER),
        m_location);
}

SqlTransaction::~SqlTransaction() noexcept
{
    switch (m_defaultMode)
    {
        case SqlTransactionMode::NONE:
            break;
        case SqlTransactionMode::COMMIT:
            // A commit that fails leaves the transaction open and autocommit off, which would hand
            // the next user of the connection - e.g. the next borrower from a pool - a transaction it
            // never started. Rolling back ends it either way.
            if (!TryCommit())
                TryRollback();
            break;
        case SqlTransactionMode::ROLLBACK:
            TryRollback();
            break;
    }
}

bool SqlTransaction::TryRollback() noexcept
{
    ZoneScopedN("SqlTransaction::TryRollback");
    m_lastError.reset();
    auto const rolledBack = Check(SQLEndTran(SQL_HANDLE_DBC, NativeHandle(), SQL_ROLLBACK), std::source_location::current());

    // A failed rollback usually means that the database already ended the transaction on its own
    // (SQLite, after an INSERT OR ROLLBACK hit its constraint), while the driver still believes it
    // open and refuses to end it. Unless the driver is brought back in line, autocommit stays off:
    // turning it on would commit whatever transaction the driver might still hold. The connection then
    // stays in manual-commit mode - TransactionActive() tells, and a pool discards such a connection.
    if (!rolledBack && !ResyncEndedTransaction())
        return false;

    if (!RestoreAutoCommit())
        return false;

    m_defaultMode = SqlTransactionMode::NONE;
    return rolledBack;
}

// Commit the transaction
bool SqlTransaction::TryCommit() noexcept
{
    ZoneScopedN("SqlTransaction::TryCommit");
    m_lastError.reset();
    if (!Check(SQLEndTran(SQL_HANDLE_DBC, NativeHandle(), SQL_COMMIT), std::source_location::current()))
    {
        // The transaction stays with the caller, to retry the commit or roll back. Should it do
        // neither, the destructor rolls back instead of trying the failed commit again.
        if (m_defaultMode == SqlTransactionMode::COMMIT)
            m_defaultMode = SqlTransactionMode::ROLLBACK;
        return false;
    }

    if (!RestoreAutoCommit())
        return false;

    m_defaultMode = SqlTransactionMode::NONE;
    return true;
}

bool SqlTransaction::Check(SQLRETURN result, std::source_location location) noexcept
{
    auto const outcome = CheckTransactionCall(result, NativeHandle(), location);
    if (outcome)
        return true;

    // The first failure of a commit or rollback is the one worth naming.
    auto error = TakeTransactionError(outcome, NativeHandle(), m_location);
    if (!m_lastError)
        m_lastError = std::move(error);
    return false;
}

bool SqlTransaction::RestoreAutoCommit() noexcept
{
    return Check(SQLSetConnectAttr(NativeHandle(), SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_IS_UINTEGER),
                 std::source_location::current());
}

bool SqlTransaction::ResyncEndedTransaction() noexcept
{
    try
    {
        // Not QueryFormatter(): a server type without a formatter (MySQL, an unrecognised one) has none.
        auto const statement = SqlQueryFormatter::ResyncEndedTransactionStatementFor(m_connection->ServerType());
        if (statement.empty())
            return false;
        auto stmt = SqlStatement { *m_connection };
        (void) stmt.ExecuteDirect(statement);
    }
    catch (...)
    {
        // The database refused to open a transaction - so one may well still be open - or the
        // connection is gone: either way the driver cannot be brought back in line.
        return false;
    }
    return SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, NativeHandle(), SQL_ROLLBACK));
}

void SqlTransaction::Rollback()
{
    if (!TryRollback())
        throw SqlTransactionException(FailureMessage("Failed to rollback the transaction", m_lastError));
}

void SqlTransaction::Commit()
{
    if (!TryCommit())
        throw SqlTransactionException(FailureMessage("Failed to commit the transaction", m_lastError));
}

} // namespace Lightweight
