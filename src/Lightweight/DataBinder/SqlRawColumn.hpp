// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Core.hpp"

#include <cstddef>
#include <span>

#include <sql.h>

namespace Lightweight
{

/// Metadata of a SQL column for use with highly optimized bulk insert operations.
struct SqlRawColumnMetadata
{
    SQLSMALLINT cType;         //!< C type of the data
    SQLSMALLINT sqlType;       //!< SQL type of the data
    SQLULEN size;              //!< Size of the data in bytes
    SQLSMALLINT decimalDigits; //!< Number of digits to the right of the decimal point
    SQLSMALLINT nullable;      //!< Whether the column is nullable
    SQLULEN bufferLength;      //!< Buffer length for variable length types
};

/// A non-owning reference to a raw column data for batch processing.
struct SqlRawColumn
{
    SqlRawColumnMetadata metadata;      //!< Metadata of the column
    std::span<std::byte const> data;    //!< Raw data of the column
    std::span<SQLLEN const> indicators; //!< Indicators of the column (SQL_NULL_DATA, SQL_DATA_AT_EXEC, etc.)
};

template <>
struct SqlDataBinder<SqlRawColumn>
{
    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             SqlRawColumn const& value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        // NOTE: For batch execution (SQL_ATTR_PARAMSET_SIZE > 1), StrLen_or_IndPtr must point to an array of indicators.
        // The driver uses the SQL_ATTR_PARAM_BIND_TYPE to determine if 'ParameterValuePtr' is an array of values
        // (Column-Wise) or a pointer to a structure (Row-Wise).
        // We assume Column-Wise binding here as we control the ExecuteBatch call.

        // For LOB types (SQL_LONGVARCHAR, SQL_LONGVARBINARY, SQL_WLONGVARCHAR), MS SQL Server's ODBC driver
        // requires the ColumnSize to be the buffer length when not using data-at-execution.
        // A size of 0 causes HY104 "Invalid precision value" error.
        SQLULEN columnSize = value.metadata.size;
        if (columnSize == 0 && (value.metadata.sqlType == SQL_LONGVARCHAR || value.metadata.sqlType == SQL_LONGVARBINARY
                                || value.metadata.sqlType == SQL_WLONGVARCHAR))
        {
            columnSize = value.metadata.bufferLength;
        }

        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                value.metadata.cType,
                                value.metadata.sqlType,
                                columnSize,
                                value.metadata.decimalDigits,
                                const_cast<SQLPOINTER>(static_cast<void const*>(value.data.data())),
                                static_cast<SQLLEN>(value.metadata.bufferLength), // element size for variable length types
                                const_cast<SQLLEN*>(value.indicators.data()));
    }
};

} // namespace Lightweight
