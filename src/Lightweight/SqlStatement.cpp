// SPDX-License-Identifier: MIT
#include "SqlStatement.hpp"

SqlStatement::SqlStatement() noexcept:
    m_ownedConnection { SqlConnection() },
    m_connection { &m_ownedConnection.value() },
    m_lastError { m_connection->LastError() }
{
    if (m_lastError == SqlError::SUCCESS)
        RequireSuccess(SQLAllocHandle(SQL_HANDLE_STMT, m_connection->NativeHandle(), &m_hStmt));
}

// Construct a new SqlStatement object, using the given connection.
SqlStatement::SqlStatement(SqlConnection& relatedConnection):
    m_connection { &relatedConnection }
{
    RequireSuccess(SQLAllocHandle(SQL_HANDLE_STMT, m_connection->NativeHandle(), &m_hStmt));
}

SqlStatement::~SqlStatement() noexcept
{
    SQLFreeHandle(SQL_HANDLE_STMT, m_hStmt);
}

void SqlStatement::Prepare(std::string_view query)
{
    SqlLogger::GetLogger().OnPrepare(query);

    m_postExecuteCallbacks.clear();
    m_postProcessOutputColumnCallbacks.clear();

    // Closes the cursor if it is open
    RequireSuccess(SQLFreeStmt(m_hStmt, SQL_CLOSE));

    // Prepares the statement
    RequireSuccess(SQLPrepareA(m_hStmt, (SQLCHAR*) query.data(), (SQLINTEGER) query.size()));
    RequireSuccess(SQLNumParams(m_hStmt, &m_expectedParameterCount));
    m_indicators.resize(m_expectedParameterCount + 1);
}

void SqlStatement::ExecuteDirect(const std::string_view& query, std::source_location location)
{
    if (query.empty())
        return;

    SqlLogger::GetLogger().OnExecuteDirect(query);

    RequireSuccess(SQLFreeStmt(m_hStmt, SQL_CLOSE), location);
    RequireSuccess(SQLExecDirectA(m_hStmt, (SQLCHAR*) query.data(), (SQLINTEGER) query.size()), location);
}

// Retrieves the number of rows affected by the last query.
size_t SqlStatement::NumRowsAffected() const
{
    SQLLEN numRowsAffected {};
    RequireSuccess(SQLRowCount(m_hStmt, &numRowsAffected));
    return numRowsAffected;
}

// Retrieves the number of columns affected by the last query.
size_t SqlStatement::NumColumnsAffected() const
{
    SQLSMALLINT numColumns {};
    RequireSuccess(SQLNumResultCols(m_hStmt, &numColumns));
    return numColumns;
}

// Retrieves the last insert ID of the last query's primary key.
size_t SqlStatement::LastInsertId()
{
    return ExecuteDirectScalar<size_t>(m_connection->Traits().LastInsertIdQuery).value_or(0);
}

// Fetches the next row of the result set.
bool SqlStatement::FetchRow()
{
    auto const sqlResult = SQLFetch(m_hStmt);
    switch (sqlResult)
    {
        case SQL_NO_DATA:
            return false;
        default:
            RequireSuccess(sqlResult);
            // post-process the output columns, if needed
            for (auto const& postProcess: m_postProcessOutputColumnCallbacks)
                postProcess();
            m_postProcessOutputColumnCallbacks.clear();
            return true;
    }
}

void SqlStatement::RequireSuccess(SQLRETURN error, std::source_location sourceLocation) const
{
    auto result = detail::UpdateSqlError(&m_lastError, error);
    if (result.has_value())
        return;

    auto errorInfo = SqlErrorInfo::fromStatementHandle(m_hStmt);
    SqlLogger::GetLogger().OnError(m_lastError, errorInfo, sourceLocation);
    if (errorInfo.sqlState == "07009")
        throw std::invalid_argument(std::format("SQL error: {}", errorInfo));
    else
        throw std::runtime_error(std::format("SQL error: {}", errorInfo));
}