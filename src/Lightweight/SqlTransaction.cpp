// SPDX-License-Identifier: Apache-2.0

#include "SqlConnection.hpp"
#include "SqlTransaction.hpp"
#include "TracyProfiler.hpp"

#include <cstdio>

namespace Lightweight
{

namespace
{
    /// Reports a failed transaction call without letting anything escape: the callers are noexcept,
    /// while building the diagnostic allocates and the installed logger is user code free to throw.
    void ReportTransactionError(SQLHDBC connection, std::source_location location) noexcept
    {
        try
        {
            SqlLogger::GetLogger().OnError(SqlErrorInfo::FromConnectionHandle(connection), location);
        }
        catch (...)
        {
            // The failure itself still reaches the caller through its return value; only the
            // report is lost, so leave a best-effort trace of that.
            std::fputs("SqlTransaction: the logger failed while reporting a transaction error.\n", stderr);
        }
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
    SQLRETURN sqlReturn = SQLEndTran(SQL_HANDLE_DBC, NativeHandle(), SQL_ROLLBACK);
    if (sqlReturn != SQL_SUCCESS && sqlReturn != SQL_SUCCESS_WITH_INFO)
    {
        ReportTransactionError(NativeHandle(), m_location);
        return false;
    }

    sqlReturn = SQLSetConnectAttr(NativeHandle(), SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_IS_UINTEGER);
    if (sqlReturn != SQL_SUCCESS && sqlReturn != SQL_SUCCESS_WITH_INFO)
    {
        ReportTransactionError(NativeHandle(), m_location);
        return false;
    }

    m_defaultMode = SqlTransactionMode::NONE;
    return true;
}

// Commit the transaction
bool SqlTransaction::TryCommit() noexcept
{
    ZoneScopedN("SqlTransaction::TryCommit");
    SQLRETURN sqlReturn = SQLEndTran(SQL_HANDLE_DBC, NativeHandle(), SQL_COMMIT);
    if (sqlReturn != SQL_SUCCESS && sqlReturn != SQL_SUCCESS_WITH_INFO)
    {
        ReportTransactionError(NativeHandle(), m_location);
        return false;
    }

    sqlReturn = SQLSetConnectAttr(NativeHandle(), SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_IS_UINTEGER);
    if (sqlReturn != SQL_SUCCESS && sqlReturn != SQL_SUCCESS_WITH_INFO)
    {
        ReportTransactionError(NativeHandle(), m_location);
        return false;
    }

    m_defaultMode = SqlTransactionMode::NONE;
    return true;
}

void SqlTransaction::Rollback()
{
    if (!TryRollback())
    {
        throw SqlTransactionException("Failed to rollback the transaction");
    }
}

void SqlTransaction::Commit()
{
    if (!TryCommit())
    {
        throw SqlTransactionException("Failed to commit the transaction");
    }
}

} // namespace Lightweight
