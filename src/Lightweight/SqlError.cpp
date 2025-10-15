// SPDX-License-Identifier: Apache-2.0
#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include "SqlError.hpp"
#include "SqlLogger.hpp"

namespace Lightweight
{

SqlException::SqlException(SqlErrorInfo info, std::source_location sourceLocation):
    std::runtime_error(std::format("{}", info)),
    _info { std::move(info) }
{
    SqlLogger::GetLogger().OnError(_info, sourceLocation);
}

void SqlErrorInfo::RequireStatementSuccess(SQLRETURN result, SQLHSTMT hStmt, std::string_view message)
{
    if (result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO) [[likely]]
        return;

    throw std::runtime_error { std::format("{}: {}", message, FromStatementHandle(hStmt)) };
}

} // namespace Lightweight
