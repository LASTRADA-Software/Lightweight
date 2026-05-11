// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "BasicStringBinder.hpp"
#include "Core.hpp"

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace Lightweight
{

/// @brief Represents a binary data type.
///
/// This class is a thin wrapper around std::vector<uint8_t> to represent binary data types efficiently.
///
/// @ingroup DataTypes
class SqlBinary final: public std::vector<uint8_t>
{
  public:
    using std::vector<uint8_t>::vector;

    /// Three-way comparison operator. Compares the byte payload only — the private
    /// `_indicator` member is ODBC bookkeeping and must not influence equality.
    constexpr auto operator<=>(SqlBinary const& other) const noexcept
    {
        return static_cast<std::vector<uint8_t> const&>(*this) <=> static_cast<std::vector<uint8_t> const&>(other);
    }

    /// Equality operator derived from the spaceship comparison above.
    constexpr bool operator==(SqlBinary const& other) const noexcept
    {
        return static_cast<std::vector<uint8_t> const&>(*this) == static_cast<std::vector<uint8_t> const&>(other);
    }

  private:
    friend struct SqlDataBinder<SqlBinary>;

    mutable SQLLEN _indicator = 0;
};

template <>
struct SqlDataBinder<SqlBinary>
{
    static constexpr auto ColumnType = SqlColumnTypeDefinitions::Binary { 255 };

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             SqlBinary const& value,
                                                             SqlDataBinderCallback& /*cb*/) noexcept
    {
        value._indicator = static_cast<SQLLEN>(value.size());
        // An empty payload binds a 1-byte sentinel buffer and `ColumnSize == 1`; the
        // indicator still carries the real byte count (0), so the server stores an
        // empty BLOB. Required because psqlODBC rejects a null `ParameterValuePtr`
        // (HY000) and the MSSQL ODBC driver rejects `ColumnSize == 0` (HY104).
        static constexpr std::uint8_t emptySentinel = 0;
        bool const isEmpty = value.empty();
        auto const* dataPtr = isEmpty ? &emptySentinel : value.data();
        SQLULEN const columnSize = isEmpty ? SQLULEN { 1 } : static_cast<SQLULEN>(value.size());
        return SQLBindParameter(stmt,
                                column,
                                SQL_PARAM_INPUT,
                                SQL_C_BINARY,
                                SQL_LONGVARBINARY,
                                columnSize,
                                0,
                                (SQLPOINTER) dataPtr,
                                0,
                                &value._indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, SqlBinary* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
    {
        if (result->empty())
            result->resize(255);

        cb.PlanPostProcessOutputColumn([stmt, column, result, indicator]() {
            if (*indicator == SQL_NULL_DATA)
                // The data is NULL.
                result->clear();
            else if (*indicator == SQL_NO_TOTAL)
                // We have a truncation and the server does not know how much data is left.
                result->resize(result->size() - 1);
            else if (std::cmp_less_equal(*indicator, static_cast<SQLLEN>(result->size())))
                result->resize(static_cast<size_t>(*indicator));
            else
            {
                // We have a truncation and the server knows how much data is left.
                // Extend the buffer and fetch the rest via SQLGetData.

                auto const totalCharsRequired = *indicator;
                result->resize(static_cast<size_t>(totalCharsRequired) + 1);
                auto const sqlResult =
                    SQLGetData(stmt, column, SQL_C_BINARY, (SQLPOINTER) result->data(), totalCharsRequired + 1, indicator);
                (void) sqlResult;
                assert(SQL_SUCCEEDED(sqlResult));
                assert(*indicator == totalCharsRequired);
                result->resize(static_cast<size_t>(totalCharsRequired));
            }
        });

        return SQLBindCol(stmt, column, SQL_C_BINARY, (SQLPOINTER) result->data(), 255, indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt,
                                                        SQLUSMALLINT column,
                                                        SqlBinary* result,
                                                        SQLLEN* indicator,
                                                        SqlDataBinderCallback const& /*cb*/) noexcept
    {
        if (result->empty())
            result->resize(255);

        return detail::GetRawColumnArrayData<SQL_C_BINARY>(stmt, column, result, indicator);
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(SqlBinary const& value)
    {
        return std::format("SqlBinary(size={})", value.size());
    }
};

} // namespace Lightweight
