// SPDX-License-Identifier: Apache-2.0

#include "SqlError.hpp"
#include "SqlLogger.hpp"
#include "Utils.hpp"

void LogIfFailed(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation)
{
    if (SQL_SUCCEEDED(error))
        return;

    SqlLogger::GetLogger().OnError(SqlErrorInfo::fromStatementHandle(hStmt), sourceLocation);
}

void RequireSuccess(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation)
{
    if (SQL_SUCCEEDED(error))
        return;

    auto errorInfo = SqlErrorInfo::fromStatementHandle(hStmt);
    if (errorInfo.sqlState == "07009")
    {
        SqlLogger::GetLogger().OnError(errorInfo, sourceLocation);
        throw std::invalid_argument(std::format("SQL error: {}", errorInfo));
    }
    else
        throw SqlException(std::move(errorInfo));
}
