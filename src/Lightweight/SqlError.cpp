// SPDX-License-Identifier: Apache-2.0

#include "DataBinder/UnicodeConverter.hpp"
#include "SqlError.hpp"
#include "SqlLogger.hpp"
#include "SqlOdbcWide.hpp"

#include <array>
#include <cstring>

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

// Collect ODBC diagnostics via the wide-character variant. Calling SQLGetDiagRecW (rather
// than the A variant) keeps us on the Unicode path the rest of the library uses, so
// driver-side diagnostic text containing non-ASCII characters round-trips without going
// through the system ANSI codepage. This function is on the failure path itself and must
// never throw — encoding errors degrade to a best-effort lossy conversion.
SqlErrorInfo SqlErrorInfo::FromHandle(SQLSMALLINT handleType, SQLHANDLE handle)
{
    SqlErrorInfo info {};

    constexpr SQLSMALLINT SqlStateLen = 6;       // 5 SQLSTATE chars + NUL
    constexpr SQLSMALLINT MessageBufferLen = 1024;
    std::array<SQLWCHAR, SqlStateLen> sqlStateBuffer {};
    std::array<SQLWCHAR, MessageBufferLen> messageBuffer {};
    SQLSMALLINT msgLen {};

    auto const rc = SQLGetDiagRecW(handleType,
                                   handle,
                                   1,
                                   sqlStateBuffer.data(),
                                   &info.nativeErrorCode,
                                   messageBuffer.data(),
                                   MessageBufferLen,
                                   &msgLen);
    if (!SQL_SUCCEEDED(rc))
        return info;

    // The SQLWCHAR ↔ char16_t layout invariant lives in SqlOdbcWide.hpp; the buffers
    // we just filled are reinterpretable as char16_t under that same assumption.
    auto const sqlStateChars = static_cast<size_t>(SqlStateLen - 1); // SQLSTATE is exactly 5 chars
    auto const sqlStateUtf8 = ToUtf8(std::u16string_view {
        reinterpret_cast<char16_t const*>(sqlStateBuffer.data()), sqlStateChars });
    info.sqlState.assign(reinterpret_cast<char const*>(sqlStateUtf8.data()), sqlStateUtf8.size());

    auto const messageChars =
        msgLen > 0 ? std::min(static_cast<SQLSMALLINT>(MessageBufferLen - 1), msgLen) : SQLSMALLINT { 0 };
    auto const messageUtf8 = ToUtf8(std::u16string_view {
        reinterpret_cast<char16_t const*>(messageBuffer.data()), static_cast<size_t>(messageChars) });
    info.message.assign(reinterpret_cast<char const*>(messageUtf8.data()), messageUtf8.size());

    return info;
}

} // namespace Lightweight
