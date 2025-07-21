// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include "../Api.hpp"
#include "../SqlServerType.hpp"

#include <concepts>
#include <functional>

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

namespace Lightweight
{

/// @defgroup DataTypes Data Types
///
/// @brief Special purpose data types for SQL data binding.

/// Callback interface for SqlDataBinder to allow post-processing of output columns.
///
/// This is needed because the SQLBindCol() function does not allow to specify a callback function to be called
/// after the data has been fetched from the database. This is needed to trim strings to the correct size, for
/// example.
class LIGHTWEIGHT_API SqlDataBinderCallback
{
  public:
    SqlDataBinderCallback() = default;
    SqlDataBinderCallback(SqlDataBinderCallback&&) = default;
    SqlDataBinderCallback(SqlDataBinderCallback const&) = default;
    SqlDataBinderCallback& operator=(SqlDataBinderCallback&&) = default;
    SqlDataBinderCallback& operator=(SqlDataBinderCallback const&) = default;

    virtual ~SqlDataBinderCallback() = default;

    virtual void PlanPostExecuteCallback(std::function<void()>&&) = 0;
    virtual void PlanPostProcessOutputColumn(std::function<void()>&&) = 0;

    [[nodiscard]] virtual SqlServerType ServerType() const noexcept = 0;
    [[nodiscard]] virtual std::string const& DriverName() const noexcept = 0;
};

template <typename>
struct SqlDataBinder;

// Default traits for output string parameters
// This needs to be implemented for each string type that should be used as output parameter via
// SqlDataBinder<>. An std::string specialization is provided below. Feel free to add more specializations for
// other string types, such as CString, etc.
template <typename>
struct SqlBasicStringOperations;

// -----------------------------------------------------------------------------------------------

namespace detail
{

    // clang-format off
template <typename T>
concept HasGetStringAndGetLength = requires(T const& t) {
    { t.GetLength() } -> std::same_as<int>;
    { t.GetString() } -> std::same_as<char const*>;
};

template <typename T>
concept HasGetStringAndLength = requires(T const& t)
{
    { t.Length() } -> std::same_as<int>;
    { t.GetString() } -> std::same_as<char const*>;
};
    // clang-format on

    template <typename>
    struct SqlViewHelper;

    template <typename T>
    concept HasSqlViewHelper = requires(T const& t) {
        { SqlViewHelper<T>::View(t) } -> std::convertible_to<std::string_view>;
    };

    template <typename CharT>
    struct SqlViewHelper<std::basic_string<CharT>>
    {
        static LIGHTWEIGHT_FORCE_INLINE std::basic_string_view<CharT> View(std::basic_string<CharT> const& str) noexcept
        {
            return { str.data(), str.size() };
        }
    };

    template <detail::HasGetStringAndGetLength CStringLike>
    struct SqlViewHelper<CStringLike>
    {
        static LIGHTWEIGHT_FORCE_INLINE std::string_view View(CStringLike const& str) noexcept
        {
            return { str.GetString(), static_cast<size_t>(str.GetLength()) };
        }
    };

    template <detail::HasGetStringAndLength StringLike>
    struct SqlViewHelper<StringLike>
    {
        static LIGHTWEIGHT_FORCE_INLINE std::string_view View(StringLike const& str) noexcept
        {
            return { str.GetString(), static_cast<size_t>(str.Length()) };
        }
    };

} // namespace detail

// -----------------------------------------------------------------------------------------------

template <typename T>
concept SqlInputParameterBinder = requires(SQLHSTMT hStmt, SQLUSMALLINT column, T const& value, SqlDataBinderCallback& cb) {
    { SqlDataBinder<T>::InputParameter(hStmt, column, value, cb) } -> std::same_as<SQLRETURN>;
};

template <typename T>
concept SqlOutputColumnBinder =
    requires(SQLHSTMT hStmt, SQLUSMALLINT column, T* result, SQLLEN* indicator, SqlDataBinderCallback& cb) {
        { SqlDataBinder<T>::OutputColumn(hStmt, column, result, indicator, cb) } -> std::same_as<SQLRETURN>;
    };

template <typename T>
concept SqlInputParameterBatchBinder =
    requires(SQLHSTMT hStmt, SQLUSMALLINT column, std::ranges::range_value_t<T>* result, SqlDataBinderCallback& cb) {
        {
            SqlDataBinder<std::ranges::range_value_t<T>>::InputParameter(
                hStmt, column, std::declval<std::ranges::range_value_t<T>>(), cb)
        } -> std::same_as<SQLRETURN>;
    };

template <typename T>
concept SqlGetColumnNativeType =
    requires(SQLHSTMT hStmt, SQLUSMALLINT column, T* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) {
        { SqlDataBinder<T>::GetColumn(hStmt, column, result, indicator, cb) } -> std::same_as<SQLRETURN>;
    };

template <typename T>
concept SqlDataBinderSupportsInspect = requires(T const& value) {
    { SqlDataBinder<std::remove_cvref_t<T>>::Inspect(value) } -> std::convertible_to<std::string>;
};

// clang-format off
template <typename StringType, typename CharType>
concept SqlBasicStringBinderConcept = requires(StringType* str) {
    { SqlBasicStringOperations<StringType>::Data(str) } -> std::same_as<CharType*>;
    { SqlBasicStringOperations<StringType>::Size(str) } -> std::same_as<SQLULEN>;
    { SqlBasicStringOperations<StringType>::Reserve(str, size_t {}) } -> std::same_as<void>;
    { SqlBasicStringOperations<StringType>::Resize(str, SQLLEN {}) } -> std::same_as<void>;
    { SqlBasicStringOperations<StringType>::Clear(str) } -> std::same_as<void>;
};
// clang-format on

} // namespace Lightweight
