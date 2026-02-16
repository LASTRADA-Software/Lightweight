// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlConnection.hpp"
#include "SqlStatement.hpp"

#include <filesystem>

namespace Lightweight
{

/// @brief Enables protocol-level ODBC trace logging for the given connection.
///
/// The trace logging is active for the lifetime of this object.
///
/// The logging output is sent to the standard output stream.
class LIGHTWEIGHT_API SqlScopedTraceLogger
{
    SQLHDBC m_nativeConnection;

  public:
    /// Constructs a scoped trace logger for the given SQL connection, logging to standard output.
    explicit SqlScopedTraceLogger(SqlConnection& connection):
        SqlScopedTraceLogger(connection.NativeHandle(),
#if defined(_WIN32) || defined(_WIN64)
                             "CONOUT$"
#else
                             "/dev/stdout"
#endif
        )
    {
    }

    /// Constructs a scoped trace logger for the given SQL statement, logging to standard output.
    explicit SqlScopedTraceLogger(SqlStatement& stmt):
        SqlScopedTraceLogger(stmt.Connection().NativeHandle(),
#if defined(_WIN32) || defined(_WIN64)
                             "CONOUT$"
#else
                             "/dev/stdout"
#endif
        )
    {
    }

    /// Constructs a scoped trace logger for the given native ODBC handle, logging to the specified file.
    explicit SqlScopedTraceLogger(SQLHDBC hDbc, std::filesystem::path const& logFile):
        m_nativeConnection { hDbc }
    {
        SQLSetConnectAttrA(m_nativeConnection, SQL_ATTR_TRACEFILE, (SQLPOINTER) logFile.string().c_str(), SQL_NTS);
        SQLSetConnectAttrA(m_nativeConnection, SQL_ATTR_TRACE, (SQLPOINTER) SQL_OPT_TRACE_ON, SQL_IS_UINTEGER);
    }

    SqlScopedTraceLogger(SqlScopedTraceLogger&&) = delete;
    SqlScopedTraceLogger& operator=(SqlScopedTraceLogger&&) = delete;
    SqlScopedTraceLogger(SqlScopedTraceLogger const&) = delete;
    SqlScopedTraceLogger& operator=(SqlScopedTraceLogger const&) = delete;

    ~SqlScopedTraceLogger()
    {
        SQLSetConnectAttrA(m_nativeConnection, SQL_ATTR_TRACE, (SQLPOINTER) SQL_OPT_TRACE_OFF, SQL_IS_UINTEGER);
    }
};

} // namespace Lightweight
