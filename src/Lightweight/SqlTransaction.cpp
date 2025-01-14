// SPDX-License-Identifier: Apache-2.0

#include "SqlConnection.hpp"
#include "SqlTransaction.hpp"

SqlTransaction::SqlTransaction(SqlConnection& connection,
                               SqlTransactionMode defaultMode,
                               std::source_location location):
    m_connection { &connection },
    m_defaultMode { defaultMode },
    m_location { location }
{
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
            TryCommit();
            break;
        case SqlTransactionMode::ROLLBACK:
            TryRollback();
            break;
    }
}

bool SqlTransaction::TryRollback() noexcept
{
    SQLRETURN sqlReturn = SQLEndTran(SQL_HANDLE_DBC, NativeHandle(), SQL_ROLLBACK);
    if (sqlReturn != SQL_SUCCESS && sqlReturn != SQL_SUCCESS_WITH_INFO)
    {
        SqlLogger::GetLogger().OnError(SqlErrorInfo::fromConnectionHandle(NativeHandle()), m_location);
        return false;
    }

    sqlReturn = SQLSetConnectAttr(NativeHandle(), SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_IS_UINTEGER);
    if (sqlReturn != SQL_SUCCESS && sqlReturn != SQL_SUCCESS_WITH_INFO)
    {
        SqlLogger::GetLogger().OnError(SqlErrorInfo::fromConnectionHandle(NativeHandle()), m_location);
        return false;
    }

    m_defaultMode = SqlTransactionMode::NONE;
    return true;
}

// Commit the transaction
bool SqlTransaction::TryCommit() noexcept
{
    SQLRETURN sqlReturn = SQLEndTran(SQL_HANDLE_DBC, NativeHandle(), SQL_COMMIT);
    if (sqlReturn != SQL_SUCCESS && sqlReturn != SQL_SUCCESS_WITH_INFO)
    {
        SqlLogger::GetLogger().OnError(SqlErrorInfo::fromConnectionHandle(NativeHandle()), m_location);
        return false;
    }

    sqlReturn = SQLSetConnectAttr(NativeHandle(), SQL_ATTR_AUTOCOMMIT, (SQLPOINTER) SQL_AUTOCOMMIT_ON, SQL_IS_UINTEGER);
    if (sqlReturn != SQL_SUCCESS && sqlReturn != SQL_SUCCESS_WITH_INFO)
    {
        SqlLogger::GetLogger().OnError(SqlErrorInfo::fromConnectionHandle(NativeHandle()), m_location);
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
