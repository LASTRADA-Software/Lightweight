// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "../Utils.hpp"
#include "Core.hpp"
#include "StringInterface.hpp"
#include "UnicodeConverter.hpp"

#include <format>
#include <limits>
#include <string>

namespace Lightweight
{

constexpr size_t SqlMaxColumnSize = (std::numeric_limits<uint32_t>::max)();

/// SQL dynamic-capacity string that mimmicks standard library string.
///
/// The underlying memory is allocated dynamically and the string can grow up to the maximum size of a specified size.
///
/// @ingroup DataTypes
template <std::size_t N, typename T = char>
class SqlDynamicString
{
  public:
    /// constexpr variable with the capacity of the string.
    static constexpr std::size_t DynamicCapacity = N;

    /// The element type of the string.
    using value_type = T;

    /// String type used for the internal storage of the string.
    using string_type = std::basic_string<T>;

    /// Iterator type for the string.
    using iterator = string_type::iterator;
    /// Const iterator type for the string.
    using const_iterator = string_type::const_iterator;
    /// Pointer type for the string.
    using pointer_type = T*;
    /// Const pointer type for the string.
    using const_pointer_type = T const*;

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

    /// Defaulted default constructor.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString() noexcept = default;

    /// Defaulted copy constructor.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString(SqlDynamicString const&) noexcept = default;

    /// Defaulted copy assignment operator.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString& operator=(SqlDynamicString const&) noexcept = default;

    /// Defaulted move constructor.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString(SqlDynamicString&&) noexcept = default;

    /// Defaulted move assignment operator.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlDynamicString& operator=(SqlDynamicString&&) noexcept = default;

    /// Defaulted destructor.
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

    /// Reserves capacity for at least the given number of characters.
    LIGHTWEIGHT_FORCE_INLINE void reserve(std::size_t capacity)
    {
        _value.reserve(capacity);
    }

    /// Appends a character to the end of the string.
    LIGHTWEIGHT_FORCE_INLINE constexpr void push_back(T c) noexcept
    {
        _value += c;
    }

    /// Removes the last character from the string.
    LIGHTWEIGHT_FORCE_INLINE constexpr void pop_back() noexcept
    {
        _value.pop_back();
    }

    /// Resizes the string to contain at most n characters. If n is less than the current size of the string,
    /// the string is reduced to its first n characters. If n is greater than the current size of the string,
    /// the string is resized to contain N characters, with the new characters being default-inserted (value-initialized).
    // NOLINTNEXTLINE(readability-identifier-naming)
    LIGHTWEIGHT_FORCE_INLINE void setsize(std::size_t n) noexcept
    {
        auto const newSize = (std::min) (n, N);
        _value.resize(newSize);
    }

    /// Resizes the string.
    LIGHTWEIGHT_FORCE_INLINE constexpr void resize(std::size_t n) noexcept
    {
        _value.resize(n);
    }

    /// Retrieves a string view of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::basic_string_view<T> substr(
        std::size_t offset = 0, std::size_t count = (std::numeric_limits<std::size_t>::max)()) const noexcept
    {
        if (count != (std::numeric_limits<std::size_t>::max)())
        {
            return _value.substr(offset, count);
        }
        return _value.substr(offset);
    }

    /// Retrieves the string as a string_type.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr string_type ToString() const noexcept
    {
        return _value;
    }

    /// Convertion operator to std::basic_string<T>.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr explicit operator std::basic_string<T>() const noexcept
    {
        return ToString();
    }

    /// Retrieves a string view of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::basic_string_view<T> ToStringView() const noexcept
    {
        return { _value.data(), _value.size() };
    }

    /// Element access operator.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr T& operator[](std::size_t index) noexcept
    {
        return _value[index];
    }

    /// Const qulified element access operator.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr T const& operator[](std::size_t index) const noexcept
    {
        return _value[index];
    }

    /// Conversion operator into std::basic_string_view<T>.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr explicit operator std::basic_string_view<T>() const noexcept
    {
        return ToStringView();
    }

    /// Three-way comparison operator for comparing with another SqlDynamicString of possibly different capacity.
    template <std::size_t OtherSize>
    LIGHTWEIGHT_FORCE_INLINE std::weak_ordering operator<=>(SqlDynamicString<OtherSize, T> const& other) const noexcept
    {
        return _value <=> other._value;
    }

    /// Equality comparison operator for comparing with another SqlDynamicString of possibly different capacity.
    template <std::size_t OtherSize>
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator==(SqlDynamicString<OtherSize, T> const& other) const noexcept
    {
        return (*this <=> other) == std::weak_ordering::equivalent;
    }

    /// Inequality comparison operator for comparing with another SqlDynamicString of possibly different capacity.
    template <std::size_t OtherSize>
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator!=(SqlDynamicString<OtherSize, T> const& other) const noexcept
    {
        return !(*this == other);
    }

    /// Equality comparison operator for comparing with a string view.
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator==(std::basic_string_view<T> other) const noexcept
    {
        return (ToStringView() <=> other) == std::weak_ordering::equivalent;
    }

    /// Inequality comparison operator for comparing with a string view.
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator!=(std::basic_string_view<T> other) const noexcept
    {
        return !(*this == other);
    }

    /// Retrieves an iterator to the beginning of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr iterator begin() noexcept
    {
        return _value.begin();
    }

    /// Retrieves an iterator to the end of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr iterator end() noexcept
    {
        return _value.end();
    }

    /// Retrieves a const iterator to the beginning of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr const_iterator begin() const noexcept
    {
        return _value.begin();
    }

    /// Retrieves a const iterator to the end of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr const_iterator end() const noexcept
    {
        return _value.end();
    }

  private:
    string_type _value;
};

static_assert(SqlStringInterface<SqlDynamicString<10>>);

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

    template <typename T>
    consteval size_t SqlMaxNumberOfChars()
    {
        // This constant defines the maximal number of bytes that can appear
        // in single column for types like varchar(max)
        // see: https://learn.microsoft.com/en-us/sql/sql-server/maximum-capacity-specifications-for-sql-server
        // 2GB or 2^31 - 1
        constexpr size_t numberOfBytes = 2147483647;
        return numberOfBytes / sizeof(T);
    }

} // namespace detail

template <typename T>
constexpr bool IsSqlDynamicString = detail::IsSqlDynamicStringImpl<T>::value;

/// Dynamic-size string of element type `char` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlDynamicAnsiString = SqlDynamicString<N, char>;

/// Dynamic-size string of element type `char16_t` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlDynamicUtf16String = SqlDynamicString<N, char16_t>;

/// Dynamic-size string of element type `char32_t` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlDynamicUtf32String = SqlDynamicString<N, char32_t>;

/// Dynamic-size string of element type `wchar_t` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlDynamicWideString = SqlDynamicString<N, wchar_t>;

/// Dynamic-size string of element type 'char' with a maximum capacity of
/// `SqlMaxNumberOfChars<char>` characters.
using SqlMaxDynamicAnsiString = SqlDynamicString<detail::SqlMaxNumberOfChars<char>(), char>;

/// Dynamic-size string of element type 'wchar_t' with a maximum capacity of
/// `SqlMaxNumberOfChars<wchar_t>` characters.
using SqlMaxDynamicWideString = SqlDynamicString<detail::SqlMaxNumberOfChars<wchar_t>(), wchar_t>;

} // namespace Lightweight

template <std::size_t N, typename T>
struct std::formatter<Lightweight::SqlDynamicString<N, T>>: std::formatter<std::string>
{
    using value_type = Lightweight::SqlDynamicString<N, T>;

    auto format(value_type const& text, format_context& ctx) const
    {
        if constexpr (Lightweight::detail::OneOf<T, wchar_t, char32_t, char16_t>)
            return std::formatter<std::string>::format((char const*) Lightweight::ToUtf8(text.ToStringView()).c_str(), ctx);
        else
            return std::formatter<std::string>::format((char const*) text.data(), ctx);
    }
};

template <std::size_t N, typename T>
struct std::formatter<std::optional<Lightweight::SqlDynamicString<N, T>>>: std::formatter<string>
{
    using value_type = std::optional<Lightweight::SqlDynamicString<N, T>>;

    auto format(value_type const& text, format_context& ctx) const
    {
        if (!text.has_value())
            return std::formatter<std::string>::format("nullopt", ctx);
        return std::formatter<std::string>::format(std::format("{}", text.value()), ctx);
    }
};

namespace Lightweight
{

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
        str->value().resize(static_cast<size_t>(indicator));
    }
};

} // namespace Lightweight
