// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

#include "../Api.hpp"
#include "../SqlServerType.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

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
    /// Default constructor.
    SqlDataBinderCallback() = default;
    /// Default move constructor.
    SqlDataBinderCallback(SqlDataBinderCallback&&) = default;
    /// Default copy constructor.
    SqlDataBinderCallback(SqlDataBinderCallback const&) = default;
    /// Default move assignment operator.
    SqlDataBinderCallback& operator=(SqlDataBinderCallback&&) = default;
    /// Default copy assignment operator.
    SqlDataBinderCallback& operator=(SqlDataBinderCallback const&) = default;

    virtual ~SqlDataBinderCallback() = default;

    /// Plans a callback to be called after the statement has been executed.
    ///
    /// @see SqlDataBinder::PostExecute()
    virtual void PlanPostExecuteCallback(std::function<void()>&&) = 0;

    /// Plans a callback to be called after a column has been processed.
    ///
    /// @see SqlDataBinder::PostProcessOutputColumn()
    virtual void PlanPostProcessOutputColumn(std::function<void()>&&) = 0;

    /// Provides a pointer to a single indicator for a single input parameter.
    ///
    /// @note The caller is responsible for filling the indicator with the length of the data or
    /// SQL_NULL_DATA.
    /// @note The indicator must remain valid until the statement is executed.
    ///
    /// @return A pointer to the indicator.
    virtual SQLLEN* ProvideInputIndicator() = 0;

    /// Provides a pointer to a contiguous array of indicators for a batch of input parameters.
    ///
    /// @note The caller is responsible for filling the indicators with the lengths of the data or
    /// SQL_NULL_DATA.
    /// @note The indicators must remain valid until the statement is executed.
    ///
    /// @param rowCount The number of rows in the batch.
    /// @return A pointer to the first element of the indicator array.
    virtual SQLLEN* ProvideInputIndicators(size_t rowCount) = 0;

    /// Provides a pointer to a contiguous, suitably aligned temporary byte buffer that remains valid
    /// until the statement is executed.
    ///
    /// This is used by batch input parameter binders that need scratch storage whose lifetime must
    /// outlive the bind call but not the execution — for example a row-strided NULL/length indicator
    /// array used by native row-wise array binding of @c std::optional columns.
    ///
    /// @note The buffer contents are unspecified; the caller is responsible for initializing it.
    /// @note The returned storage is aligned to at least @c alignof(std::max_align_t).
    /// @note Row-wise callers request @c ~rowStride*rowCount bytes to hold only @c rowCount @c SQLLEN
    /// indicators. This over-allocation is intrinsic to ODBC row-wise binding, which strides the
    /// @c StrLen_or_IndPtr array by @c SQL_ATTR_PARAM_BIND_TYPE (the row stride) — there is no separate
    /// indicator stride — so a tightly packed indicator array would require descriptor-level binding.
    ///
    /// @param byteCount The number of bytes to provide.
    /// @return A pointer to the first byte of the buffer.
    virtual std::byte* ProvideBatchStagingBuffer(std::size_t byteCount) = 0;

    /// @return The server type of the database.
    [[nodiscard]] virtual SqlServerType ServerType() const noexcept = 0;

    /// @return The driver name of the database.
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

    /// @brief Satisfied when @p T is the same type as at least one of @p Us.
    ///
    /// Mirrors @c Lightweight::detail::OneOf, but lives in the DataBinder layer so the low-level binder
    /// headers can use it without reaching up to the higher-level Utils.hpp.
    template <typename T, typename... Us>
    concept IsAnyOf = (std::same_as<T, Us> || ...);

    /// @brief Byte offset of the contained value within a @c std::optional<T>.
    ///
    /// Used by the row-wise batch binders to address the inner value of each row's optional in place. The
    /// offset is 0 on all known standard libraries, but is computed rather than assumed so the address
    /// arithmetic does not bake in that assumption. It is derived from the integer addresses of a probe
    /// optional and its contained value (rather than @c byte* subtraction) to keep the computation defined.
    ///
    /// @tparam T The contained value type.
    /// @return The offset, in bytes, of the contained value within the optional.
    template <typename T>
    [[nodiscard]] inline std::size_t OptionalValueOffset() noexcept
    {
        std::optional<T> const probe { T {} };
        return static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(std::addressof(*probe))
                                        - reinterpret_cast<std::uintptr_t>(std::addressof(probe)));
    }

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

/// @brief Opt-in trait marking a value type as bindable in a native ODBC row-wise parameter array.
///
/// A type qualifies when it is fixed-width, stored inline, bound via a plain @c SQLBindParameter with
/// no per-call heap conversion, and identically across all supported backends — so its address can be
/// handed to ODBC and strided by @c SQL_ATTR_PARAM_BIND_TYPE. The primary template is @c false; each
/// eligible binder header specializes it to @c true (primitives, date/time/datetime, numeric).
///
/// @note @c SqlGuid is intentionally NOT marked: on SQLite it is bound via a per-value text conversion,
/// which cannot be expressed as a zero-copy row-wise array — GUID columns use the soft batch path.
template <typename T>
inline constexpr bool SqlIsNativeRowBindableValue = false;

/// @brief Opt-in trait marking a value type as an @c SqlNumeric specialization.
///
/// Numeric values are row-wise bindable, but @c std::optional<SqlNumeric> is not (the contained value
/// is not bound at a uniform offset/representation across backends), so the optional batch path
/// excludes them via this trait.
template <typename T>
inline constexpr bool SqlIsNumericValue = false;

/// @brief Whether @p T is a @c std::optional specialization.
///
/// Defined locally rather than reusing @c Lightweight::IsSpecializationOf (Utils.hpp) or the DataMapper
/// @c IsStdOptional (Field.hpp): this low-level binder header is deliberately kept free of dependencies on
/// those higher-level headers, so it carries its own minimal optional traits.
template <typename T>
inline constexpr bool SqlIsStdOptional = false;

template <typename T>
inline constexpr bool SqlIsStdOptional<std::optional<T>> = true;

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
