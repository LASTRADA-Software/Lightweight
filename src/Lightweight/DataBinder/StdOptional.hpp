// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Core.hpp"
#include "SqlNullValue.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <string>

namespace Lightweight
{

namespace detail
{
    /// Whether @p T's binder exposes an optional-aware row-wise batch binder
    /// (@c BatchRowWiseInputParameterOptional). True for length-bearing inline inner types
    /// (fixed-capacity strings), which need a per-row length indicator; false for plain fixed-width inner
    /// types (primitives, date/time), which carry only a NULL indicator and bind via
    /// @c BatchInputParameter.
    template <typename T>
    concept SqlHasOptionalRowWiseBatchBinder = requires(
        SQLHSTMT stmt, SQLUSMALLINT column, std::optional<T> const* elem0, std::size_t rowCount, SqlDataBinderCallback& cb) {
        SqlDataBinder<T>::BatchRowWiseInputParameterOptional(stmt, column, elem0, rowCount, rowCount, cb);
    };
} // namespace detail

template <typename T>
struct SqlDataBinder<std::optional<T>>
{
    using OptionalValue = std::optional<T>;

    static constexpr auto ColumnType = SqlDataBinder<T>::ColumnType;

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN InputParameter(SQLHSTMT stmt,
                                                             SQLUSMALLINT column,
                                                             OptionalValue const& value,
                                                             SqlDataBinderCallback& cb) noexcept
    {
        if (value.has_value())
            return SqlDataBinder<T>::InputParameter(stmt, column, *value, cb);
        else
            return SqlDataBinder<SqlNullType>::InputParameter(stmt, column, SqlNullValue, cb);
    }

    /// Binds an optional column of a native row-wise batch zero-copy, supplying per-row NULL-ness via a
    /// temporary row-strided indicator buffer.
    ///
    /// Dispatches on the inner type @p T:
    /// - Length-bearing inline types (fixed-capacity strings): delegates to @p T's binder, which fills
    ///   the per-row length-or-@c SQL_NULL_DATA indicator and binds the inline character buffers.
    /// - Plain fixed-width types (primitives, date/time): the contained value of each row is bound in
    ///   place (no copy); the driver strides by the statement's @c SQL_ATTR_PARAM_BIND_TYPE (== @p
    ///   rowStride). For rows whose optional is disengaged the indicator is @c SQL_NULL_DATA so the
    ///   (indeterminate) value bytes are ignored — the value storage is never dereferenced as a @c T,
    ///   only its address is taken.
    ///
    /// @note PRE (guaranteed by the caller): the statement is in row-wise binding mode with
    /// @c SQL_ATTR_PARAM_BIND_TYPE == @p rowStride, and @c rowStride % alignof(SQLLEN) == 0 so the
    /// indicator slots at @c i*rowStride stay aligned and non-overlapping.
    ///
    /// @param stmt The ODBC statement handle.
    /// @param column The 1-based parameter column index.
    /// @param elem0 Address of the @c std::optional<T> within row 0 of the contiguous record array.
    /// @param rowStride The row-wise bind stride (== sizeof of the row element).
    /// @param rowCount The number of rows in the batch.
    /// @param cb The data binder callback (provides the temporary indicator buffer).
    /// @return The ODBC return code of the underlying bind.
    static SQLRETURN BatchRowWiseInputParameter(SQLHSTMT stmt,
                                                SQLUSMALLINT column,
                                                OptionalValue const* elem0,
                                                std::size_t rowStride,
                                                std::size_t rowCount,
                                                SqlDataBinderCallback& cb) noexcept
    {
        if constexpr (detail::SqlHasOptionalRowWiseBatchBinder<T>)
        {
            // Length-bearing inline inner type (fixed-capacity string): its binder owns the
            // NULL-or-length indicator fill and the zero-copy bind.
            return SqlDataBinder<T>::BatchRowWiseInputParameterOptional(stmt, column, elem0, rowStride, rowCount, cb);
        }
        else
        {
            // Offset of the contained value within std::optional<T> (0 on all known standard libraries,
            // but computed robustly so the address arithmetic below does not bake in that assumption).
            // Derived from the integer addresses rather than via pointer subtraction: the contained value
            // and the optional are distinct objects, so `byte* - byte*` would be undefined behaviour, whereas
            // subtracting their `std::uintptr_t` addresses is well-defined here.
            OptionalValue const probe { T {} };
            auto const valueOffset = reinterpret_cast<std::uintptr_t>(std::addressof(*probe))
                                     - reinterpret_cast<std::uintptr_t>(std::addressof(probe));

            // Row-strided indicator buffer: ODBC reads the indicator for row i at base + i*rowStride. The
            // optionals themselves are embedded in the row structs, so they are also addressed at
            // sourceBytes + i*rowStride (NOT elem0[i], which would stride by sizeof(std::optional<T>)).
            auto const* sourceBytes = reinterpret_cast<std::byte const*>(elem0);
            auto* const indicatorBytes = cb.ProvideBatchStagingBuffer(((rowCount - 1) * rowStride) + sizeof(SQLLEN));
            for (auto const i: std::views::iota(std::size_t { 0 }, rowCount))
            {
                // The optional and its indicator slot for row i both live at offset i*rowStride (row-wise).
                auto const& optional = *reinterpret_cast<OptionalValue const*>(sourceBytes + (i * rowStride));
                auto* const indicatorSlot = reinterpret_cast<SQLLEN*>(indicatorBytes + (i * rowStride));
                *indicatorSlot = optional.has_value() ? SQLLEN { 0 } : SQLLEN { SQL_NULL_DATA };
            }

            // Zero-copy value bind: point at row 0's contained value; T's batch binder issues the
            // SQLBindParameter (taking only the address, never forming a T& to possibly-disengaged storage).
            auto const* valueBase = reinterpret_cast<T const*>(reinterpret_cast<std::byte const*>(elem0) + valueOffset);
            return SqlDataBinder<T>::BatchInputParameter(
                stmt, column, valueBase, rowCount, cb, reinterpret_cast<SQLLEN*>(indicatorBytes));
        }
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN OutputColumn(
        SQLHSTMT stmt, SQLUSMALLINT column, OptionalValue* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
    {
        if (!result)
            return SQL_ERROR;

        auto const sqlReturn = SqlDataBinder<T>::OutputColumn(stmt, column, &result->emplace(), indicator, cb);
        cb.PlanPostProcessOutputColumn([result, indicator]() {
            if (indicator && *indicator == SQL_NULL_DATA)
                *result = std::nullopt;
        });
        return sqlReturn;
    }

    static LIGHTWEIGHT_FORCE_INLINE SQLRETURN GetColumn(SQLHSTMT stmt,
                                                        SQLUSMALLINT column,
                                                        OptionalValue* result,
                                                        SQLLEN* indicator,
                                                        SqlDataBinderCallback const& cb) noexcept
    {
        auto const sqlReturn = SqlDataBinder<T>::GetColumn(stmt, column, &result->emplace(), indicator, cb);
        if (indicator && *indicator == SQL_NULL_DATA)
            *result = std::nullopt;
        return sqlReturn;
    }

    static LIGHTWEIGHT_FORCE_INLINE std::string Inspect(OptionalValue const& value) noexcept
    {
        if (!value)
            return "NULL";
        else
            return std::string(SqlDataBinder<T>::Inspect(*value));
    }
};

} // namespace Lightweight
