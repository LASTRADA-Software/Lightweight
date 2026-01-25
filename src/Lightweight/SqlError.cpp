// SPDX-License-Identifier: Apache-2.0

#include "SqlError.hpp"
#include "SqlLogger.hpp"

namespace Lightweight
{

SqlException::SqlException(SqlErrorInfo info, std::source_location /*sourceLocation*/):
    std::runtime_error(std::format("{}", info)),
    _info { std::move(info) }
{
    // Note: We don't log here because the exception will be thrown and handled by the caller.
    // Logging should happen at the point where errors are silently handled, not where they're thrown.
}

void SqlErrorInfo::RequireStatementSuccess(SQLRETURN result, SQLHSTMT hStmt, std::string_view message)
{
    if (result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO) [[likely]]
        return;

    throw std::runtime_error { std::format("{}: {}", message, FromStatementHandle(hStmt)) };
}

} // namespace Lightweight
