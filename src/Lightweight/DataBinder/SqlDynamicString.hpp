// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "../Utils.hpp"
#include "Core.hpp"
#include "UnicodeConverter.hpp"

#include <format>
#include <limits>
#include <string>

constexpr size_t SqlMaxColumnSize = (std::numeric_limits<uint32_t>::max)();

/// SQL dynamic-capacity string that mimmicks standard library string.
///
/// The underlying memory is allocated dynamically and the string can grow up to the maximum size of a SQL column.
///
/// @ingroup DataTypes
template <std::size_t N, typename T = char>
class SqlDynamicString
{
  public:
    static constexpr std::size_t DynamicCapacity = N;
    using value_type = T;
    using string_type = std::basic_string<T>;

    /// Constructs a fixed-size string from a string literal.
    template <std::size_t SourceSize>
    constexpr LIGHTWEIGHT_FORCE_INLINE SqlDynamicString(T const (&text)[SourceSize]):
        _value { text, SourceSize - 1 }
    {
        static_assert(SourceSize <= N + 1, "RHS string size must not exceed target string's capacity.");
    }

    /// Constructs a fixed-size string from a string pointer and end pointer.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString(T const* s, T const* e) noexcept:
        _value { s, e }
    {
    }

    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString() noexcept = default;
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString(SqlDynamicString const&) noexcept = default;
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString& operator=(SqlDynamicString const&) noexcept = default;
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString(SqlDynamicString&&) noexcept = default;
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString& operator=(SqlDynamicString&&) noexcept = default;
    LIGHTWEIGHT_FORCE_INLINE constexpr ~SqlDynamicString() noexcept = default;

    /// Constructs a fixed-size string from a string view.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString(std::basic_string_view<T> s) noexcept:
        _value { s }
    {
    }

    /// Constructs a fixed-size string from a string.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString(std::basic_string<T> const& s) noexcept:
        _value { s }
    {
    }

    /// Returns a string view of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::basic_string_view<T> str() const noexcept
    {
        return std::basic_string_view<T> { data(), size() };
    }

    /// Retrieves the string's inner value (std::basic_string<T>).
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE string_type const& value() const noexcept
    {
        return _value;
    }

    /// Retrieves the string's inner value (std::basic_string<T>).
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE string_type& value() noexcept
    {
        return _value;
    }

    /// Retrieves the string's inner value (as T const*).
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE T const* data() const noexcept
    {
        return _value.data();
    }

    /// Retrieves the string's inner value (as T*).
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE T* data() noexcept
    {
        return _value.data();
    }

    /// Retrieves the string's inner value (as T const*).
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE T const* c_str() const noexcept
    {
        return _value.c_str();
    }

    /// Retrieves the string's capacity.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::size_t capacity() const noexcept
    {
        return N;
    }

    /// Retrieves the string's size.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE std::size_t size() const noexcept
    {
        return _value.size();
    }

    /// Tests if the string is empty.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr bool empty() const noexcept
    {
        return _value.empty();
    }

    /// Clears the string.
    LIGHTWEIGHT_FORCE_INLINE constexpr void clear() noexcept
    {
        _value.clear();
    }

    /// Resizes the string.
    LIGHTWEIGHT_FORCE_INLINE constexpr void resize(std::size_t n) noexcept
    {
        _value.resize(n);
    }

    /// Retrieves a string view of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::basic_string_view<T> ToStringView() const noexcept
    {
        return { _value.data(), _value.size() };
    }

    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr T& operator[](std::size_t index) noexcept
    {
        return _value[index];
    }

    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr T const& operator[](std::size_t index) const noexcept
    {
        return _value[index];
    }

    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr explicit operator std::basic_string_view<T>() const noexcept
    {
        return ToStringView();
    }

    template <std::size_t OtherSize>
    LIGHTWEIGHT_FORCE_INLINE std::weak_ordering operator<=>(SqlDynamicString<OtherSize, T> const& other) const noexcept
    {
        return _value <=> other._value;
    }

    template <std::size_t OtherSize>
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator==(SqlDynamicString<OtherSize, T> const& other) const noexcept
    {
        return (*this <=> other) == std::weak_ordering::equivalent;
    }

    template <std::size_t OtherSize>
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator!=(SqlDynamicString<OtherSize, T> const& other) const noexcept
    {
        return !(*this == other);
    }

    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator==(std::basic_string_view<T> other) const noexcept
    {
        return (ToStringView() <=> other) == std::weak_ordering::equivalent;
    }

    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator!=(std::basic_string_view<T> other) const noexcept
    {
        return !(*this == other);
    }

  private:
    string_type _value;
};

template <std::size_t N, typename CharT>
struct detail::SqlViewHelper<SqlDynamicString<N, CharT>>
{
    static LIGHTWEIGHT_FORCE_INLINE std::basic_string_view<CharT> View(SqlDynamicString<N, CharT> const& str) noexcept
    {
        return { str.data(), str.size() };
    }
};

namespace detail
{

template <typename>
struct IsSqlDynamicStringImpl: std::false_type
{
};

template <std::size_t N, typename T>
struct IsSqlDynamicStringImpl<SqlDynamicString<N, T>>: std::true_type
{
};

template <std::size_t N, typename T>
struct IsSqlDynamicStringImpl<std::optional<SqlDynamicString<N, T>>>: std::true_type
{
};

} // namespace detail

template <typename T>
constexpr bool IsSqlDynamicString = detail::IsSqlDynamicStringImpl<T>::value;

/// Fixed-size string of element type `char` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlDynamicAnsiString = SqlDynamicString<N, char>;

/// Fixed-size string of element type `char16_t` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlDynamicUtf16String = SqlDynamicString<N, char16_t>;

/// Fixed-size string of element type `char32_t` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlDynamicUtf32String = SqlDynamicString<N, char32_t>;

/// Fixed-size string of element type `wchar_t` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlDynamicWideString = SqlDynamicString<N, wchar_t>;

template <std::size_t N, typename T>
struct std::formatter<SqlDynamicString<N, T>>: std::formatter<std::string>
{
    using value_type = SqlDynamicString<N, T>;

    auto format(value_type const& text, format_context& ctx) const
    {
        if constexpr (detail::OneOf<T, wchar_t, char32_t, char16_t>)
            return std::formatter<std::string>::format((char const*) ToUtf8(text.ToStringView()).c_str(), ctx);
        else
            return std::formatter<std::string>::format((char const*) text.data(), ctx);
    }
};

template <std::size_t N, typename T>
struct SqlBasicStringOperations<SqlDynamicString<N, T>>
{
    using CharType = T;
    using ValueType = SqlDynamicString<N, CharType>;

    static constexpr SqlColumnTypeDefinition ColumnType = []() constexpr {
        if constexpr (std::same_as<CharType, char>)
            return SqlColumnTypeDefinitions::Varchar { N };
        else
            return SqlColumnTypeDefinitions::NVarchar { N };
    }();

    static CharType const* Data(ValueType const* str) noexcept
    {
        return str->data();
    }

    static CharType* Data(ValueType* str) noexcept
    {
        return str->data();
    }

    static SQLULEN Size(ValueType const* str) noexcept
    {
        return str->size();
    }

    static void Clear(ValueType* str) noexcept
    {
        str->value().clear();
    }

    static void Reserve(ValueType* str, size_t capacity) noexcept
    {
        str->value().resize((std::min) (N, capacity));
    }

    static void Resize(ValueType* str, SQLLEN indicator) noexcept
    {
        str->value().resize(indicator);
    }
};
