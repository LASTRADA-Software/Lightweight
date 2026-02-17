// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../SqlColumnTypeDefinitions.hpp"
#include "Core.hpp"
#include "StringInterface.hpp"
#include "UnicodeConverter.hpp"

#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace Lightweight
{

/// Enum to define the behavior of SqlFixedString after retrieval from the database, in case the retrieved string is shorter
/// than the fixed capacity.
enum class SqlFixedStringMode : uint8_t
{
    /// The string is treated as fixed-size, and the size is always set to the fixed capacity.
    FIXED_SIZE,
    /// The string is treated as fixed-size, but the size is set to the actual size of the retrieved string. If the
    /// retrieved string is shorter than the fixed capacity, the remaining characters are right-trimmed and the size is set
    /// to the actual size of the retrieved string.
    FIXED_SIZE_RIGHT_TRIMMED,
    /// The string is treated as variable-size, and the size is set to the actual size of the retrieved string.
    VARIABLE_SIZE,
};

/// SQL fixed-capacity string that mimmicks standard library string/string_view with a fixed-size underlying
/// buffer.
///
/// The underlying storage will not be guaranteed to be `\0`-terminated unless
/// a call to mutable/const c_str() has been performed.
///
/// @ingroup DataTypes
template <std::size_t N, typename T = char, SqlFixedStringMode Mode = SqlFixedStringMode::FIXED_SIZE>
class SqlFixedString
{
  private:
    T _data[N + 1] {};
    std::size_t _size = 0;

  public:
    /// The element type of the string.
    using value_type = T;

    /// Iterator type for the string.
    using iterator = T*;

    /// Const iterator type for the string.
    using const_iterator = T const*;

    /// Pointer type for the string.
    using pointer_type = T*;

    /// Const pointer type for the string.
    using const_pointer_type = T const*;

    /// The specified width of the string, which is also the maximum size of the string.
    static constexpr std::size_t Capacity = N;

    /// Handling mode for the post-retrieval operation that defines how the string
    /// should be treated when it is retrieved from the database.
    static constexpr SqlFixedStringMode PostRetrieveOperation = Mode;

    /// Constructs a fixed-size string from a string literal.
    template <std::size_t SourceSize>
    constexpr LIGHTWEIGHT_FORCE_INLINE SqlFixedString(T const (&text)[SourceSize]):
        _size { SourceSize - 1 }
    {
        static_assert(SourceSize <= N + 1, "RHS string size must not exceed target string's capacity.");
        std::copy_n(text, SourceSize, _data);
    }

    /// Defaulted default constructor.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlFixedString() noexcept = default;

    /// Defaulted copy constructor.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlFixedString(SqlFixedString const&) noexcept = default;

    /// Defaulted copy assignment operator.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlFixedString& operator=(SqlFixedString const&) noexcept = default;

    /// Defaulted move constructor.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlFixedString(SqlFixedString&&) noexcept = default;

    /// Defaulted move assignment operator.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlFixedString& operator=(SqlFixedString&&) noexcept = default;

    /// Defaulted destructor.
    LIGHTWEIGHT_FORCE_INLINE constexpr ~SqlFixedString() noexcept = default;

    /// Constructs a fixed-size string from a string view.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlFixedString(std::basic_string_view<T> s) noexcept:
        _size { (std::min) (N, s.size()) }
    {
        std::copy_n(s.data(), _size, _data); // NOLINT(bugprone-suspicious-stringview-data-usage)
    }

    /// Constructs a fixed-size string from a string.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlFixedString(std::basic_string<T> const& s) noexcept:
        _size { (std::min) (N, s.size()) }
    {
        std::copy_n(s.data(), _size, _data);
    }

    /// Constructs a fixed-size string from a string pointer and length.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlFixedString(T const* s, std::size_t len) noexcept:
        _size { (std::min) (N, len) }
    {
        std::copy_n(s, _size, _data);
    }

    /// Constructs a fixed-size string from a string pointer and end pointer.
    LIGHTWEIGHT_FORCE_INLINE constexpr SqlFixedString(T const* s, T const* e) noexcept:
        _size { (std::min) (N, static_cast<std::size_t>(e - s)) }
    {
        std::copy(s, e, _data);
    }

    /// Reserves capacity for the string. Throws if capacity exceeds the maximum.
    LIGHTWEIGHT_FORCE_INLINE void reserve(std::size_t capacity)
    {
        if (capacity > N)
            throw std::length_error(std::format("SqlFixedString: capacity {} exceeds maximum capacity {}", capacity, N));
    }

    /// Tests if the string is empty.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr bool empty() const noexcept
    {
        return _size == 0;
    }

    /// Returns the size of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::size_t size() const noexcept
    {
        return _size;
    }

    /// Sets the size of the string, capped at the maximum capacity.
    // NOLINTNEXTLINE(readability-identifier-naming)
    LIGHTWEIGHT_FORCE_INLINE /*TODO constexpr*/ void setsize(std::size_t n) noexcept
    {
        auto const newSize = (std::min) (n, N);
        _size = newSize;
        _data[newSize] = '\0';
    }

    /// Resizes the string.
    ///
    /// This sets the size of the string to `n`. If `n` is greater than the current size,
    /// capped at the maximum capacity.
    LIGHTWEIGHT_FORCE_INLINE constexpr void resize(std::size_t n) noexcept
    {
        auto const newSize = (std::min) (n, N);
        _size = newSize;
        _data[newSize] = '\0';
    }

    /// Returns the maximum capacity of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::size_t capacity() const noexcept
    {
        return N;
    }

    /// Clears the string.
    LIGHTWEIGHT_FORCE_INLINE constexpr void clear() noexcept
    {
        _size = 0;
    }

    // /// Assigns a string literal to the string.
    // template <std::size_t SourceSize>
    // LIGHTWEIGHT_FORCE_INLINE constexpr void assign(T const (&source)[SourceSize]) noexcept
    // {
    //     static_assert(SourceSize <= N + 1, "Source string must not overflow the target string's capacity.");
    //     _size = SourceSize - 1;
    //     std::copy_n(source, SourceSize, _data);
    // }

    // /// Assigns a string view to the string.
    // LIGHTWEIGHT_FORCE_INLINE constexpr void assign(std::basic_string_view<T> s) noexcept
    // {
    //     _size = (std::min) (N, s.size());
    //     std::copy_n(s.data(), _size, _data); // NOLINT(bugprone-suspicious-stringview-data-usage)
    // }

    /// Appends a character to the string.
    LIGHTWEIGHT_FORCE_INLINE constexpr void push_back(T c) noexcept
    {
        if (_size < N)
        {
            _data[_size] = c;
            ++_size;
        }
    }

    /// Removes the last character from the string.
    LIGHTWEIGHT_FORCE_INLINE constexpr void pop_back() noexcept
    {
        if (_size > 0)
            --_size;
    }

    /// Returns a sub string of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::basic_string_view<T> substr(
        std::size_t offset = 0, std::size_t count = (std::numeric_limits<std::size_t>::max)()) const noexcept
    {
        if (offset >= _size)
            return {};
        if (count == (std::numeric_limits<std::size_t>::max)())
            return std::basic_string_view<T>(_data + offset, _size - offset);
        if (offset + count > _size)
            return std::basic_string_view<T>(_data + offset, _size - offset);
        return std::basic_string_view<T>(_data + offset, count);
    }

    /// Returns a string view of the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::basic_string_view<T> str() const noexcept
    {
        return std::basic_string_view<T> { _data, _size };
    }

    // clang-format off
    /// Returns a pointer to the underlying mutable data.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr pointer_type data() noexcept { return _data; }
    /// Returns a mutable iterator to the beginning.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr iterator begin() noexcept { return _data; }
    /// Returns a mutable iterator to the end.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr iterator end() noexcept { return _data + size(); }
    /// Returns a mutable reference to the element at the given index.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr T& operator[](std::size_t i) noexcept { return _data[i]; }

    /// Returns a pointer to the null-terminated C string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr const_pointer_type c_str() const noexcept { return _data; }
    /// Returns a const pointer to the underlying data.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr const_pointer_type data() const noexcept { return _data; }
    /// Returns a const iterator to the beginning.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr const_iterator begin() const noexcept { return _data; }
    /// Returns a const iterator to the end.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr const_iterator end() const noexcept { return _data + size(); }
    /// Returns a const reference to the element at the given index.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr T const& operator[](std::size_t i) const noexcept { return _data[i]; }
    // clang-format on

    /// Returns a std::basic_string<T> from the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::basic_string<T> ToString() const noexcept
    {
        return { _data, _size };
    }

    /// Returns a std::basic_string<T> from the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr explicit operator std::basic_string<T>() const noexcept
    {
        return ToString();
    }

    /// Returns a std::basic_string_view<T> from the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr std::basic_string_view<T> ToStringView() const noexcept
    {
        return { _data, _size };
    }

    /// Returns a std::basic_string_view<T> from the string.
    [[nodiscard]] LIGHTWEIGHT_FORCE_INLINE constexpr explicit operator std::basic_string_view<T>() const noexcept
    {
        return ToStringView();
    }

    /// Three-way comparison operator.
    template <std::size_t OtherSize, SqlFixedStringMode OtherMode>
    LIGHTWEIGHT_FORCE_INLINE std::weak_ordering operator<=>(
        SqlFixedString<OtherSize, T, OtherMode> const& other) const noexcept
    {
        if ((void*) this == (void*) &other) [[unlikely]]
            return std::weak_ordering::equivalent;

        for (auto const i: std::views::iota(0U, (std::min) (size(), other.size())))
            if (auto const cmp = _data[i] <=> other._data[i]; cmp != std::weak_ordering::equivalent) [[unlikely]]
                return cmp;
        return size() <=> other.size();
    }

    /// Equality comparison operator.
    template <std::size_t OtherSize, SqlFixedStringMode OtherMode>
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator==(SqlFixedString<OtherSize, T, OtherMode> const& other) const noexcept
    {
        return (*this <=> other) == std::weak_ordering::equivalent;
    }

    /// Inequality comparison operator.
    template <std::size_t OtherSize, SqlFixedStringMode OtherMode>
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator!=(SqlFixedString<OtherSize, T, OtherMode> const& other) const noexcept
    {
        return !(*this == other);
    }

    /// Equality comparison with a string view.
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator==(std::basic_string_view<T> other) const noexcept
    {
        return (substr() <=> other) == std::weak_ordering::equivalent;
    }

    /// Inequality comparison with a string view.
    LIGHTWEIGHT_FORCE_INLINE constexpr bool operator!=(std::basic_string_view<T> other) const noexcept
    {
        return !(*this == other);
    }
};

static_assert(SqlStringInterface<SqlFixedString<10>>);

template <std::size_t N, typename CharT, SqlFixedStringMode Mode>
struct detail::SqlViewHelper<SqlFixedString<N, CharT, Mode>>
{
    static LIGHTWEIGHT_FORCE_INLINE std::basic_string_view<CharT> View(SqlFixedString<N, CharT, Mode> const& str) noexcept
    {
        return { str.data(), str.size() };
    }
};

namespace detail
{

    template <typename>
    struct IsSqlFixedStringTypeImpl: std::false_type
    {
    };

    template <std::size_t N, typename T, SqlFixedStringMode Mode>
    struct IsSqlFixedStringTypeImpl<SqlFixedString<N, T, Mode>>: std::true_type
    {
    };

} // namespace detail

template <typename T>
constexpr bool IsSqlFixedString = detail::IsSqlFixedStringTypeImpl<T>::value;

/// Fixed-size string of element type `char` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlAnsiString = SqlFixedString<N, char, SqlFixedStringMode::VARIABLE_SIZE>;

/// Fixed-size string of element type `char16_t` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlUtf16String = SqlFixedString<N, char16_t, SqlFixedStringMode::VARIABLE_SIZE>;

/// Fixed-size string of element type `char32_t` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlUtf32String = SqlFixedString<N, char32_t, SqlFixedStringMode::VARIABLE_SIZE>;

/// Fixed-size string of element type `wchar_t` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlWideString = SqlFixedString<N, wchar_t, SqlFixedStringMode::VARIABLE_SIZE>;

/// Fixed-size (right-trimmed) string of element type `char` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlTrimmedFixedString = SqlFixedString<N, char, SqlFixedStringMode::FIXED_SIZE_RIGHT_TRIMMED>;

/// Fixed-size (right-trimmed) string of element type `wchar_t` with a capacity of `N` characters.
///
/// @ingroup DataTypes
template <std::size_t N>
using SqlTrimmedWideFixedString = SqlFixedString<N, wchar_t, SqlFixedStringMode::FIXED_SIZE_RIGHT_TRIMMED>;

template <std::size_t N, typename T = char>
using SqlString = SqlFixedString<N, T, SqlFixedStringMode::VARIABLE_SIZE>;

template <std::size_t N, typename T, SqlFixedStringMode Mode>
struct SqlBasicStringOperations<SqlFixedString<N, T, Mode>>
{
    using CharType = T;
    using ValueType = SqlFixedString<N, CharType, Mode>;
    static constexpr auto ColumnType = []() constexpr -> SqlColumnTypeDefinition {
        if constexpr (std::same_as<CharType, char>)
        {
            if constexpr (Mode == SqlFixedStringMode::VARIABLE_SIZE)
                return SqlColumnTypeDefinitions::Varchar { N };
            else
                return SqlColumnTypeDefinitions::Char { N };
        }
        else
        {
            if constexpr (Mode == SqlFixedStringMode::VARIABLE_SIZE)
                return SqlColumnTypeDefinitions::NVarchar { N };
            else
                return SqlColumnTypeDefinitions::NChar { N };
        }
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
        str->clear();
    }

    static void Reserve(ValueType* str, size_t capacity) noexcept
    {
        str->reserve((std::min) (N, capacity));
        str->resize((std::min) (N, capacity));
    }

    static void Resize(ValueType* str, SQLLEN indicator) noexcept
    {
        str->resize(static_cast<size_t>(indicator));
    }

    static void PostProcessOutputColumn(ValueType* result, SQLLEN indicator)
    {
        switch (indicator)
        {
            case SQL_NULL_DATA:
                result->clear();
                break;
            case SQL_NO_TOTAL:
                result->resize(N);
                break;
            default: {
                auto const len = (std::min) (N, static_cast<std::size_t>(indicator) / sizeof(CharType));
                result->setsize(len);

                if constexpr (Mode == SqlFixedStringMode::FIXED_SIZE_RIGHT_TRIMMED)
                {
                    TrimRight(result, indicator);
                }
                else
                {
                    // Strip trailing null characters for all modes
                    StripTrailingNulls(result);
                }
                break;
            }
        }
    }

    LIGHTWEIGHT_FORCE_INLINE static void StripTrailingNulls(ValueType* str) noexcept
    {
        size_t n = str->size();
        while (n > 0 && (*str)[n - 1] == '\0')
            --n;
        str->setsize(n);
    }

    LIGHTWEIGHT_FORCE_INLINE static void TrimRight(ValueType* boundOutputString, SQLLEN indicator) noexcept
    {
#if defined(_WIN32)
        size_t n = (std::min) (static_cast<size_t>(indicator) / sizeof(CharType), N - 1);
#else
        size_t n = std::min(static_cast<size_t>(indicator), N - 1);
#endif
        // Strip trailing whitespace and null characters
        while (n > 0 && (std::isspace((*boundOutputString)[n - 1]) || (*boundOutputString)[n - 1] == '\0'))
            --n;
        boundOutputString->setsize(n);
    }
};

} // namespace Lightweight

template <std::size_t N, typename T, Lightweight::SqlFixedStringMode P>
struct std::formatter<Lightweight::SqlFixedString<N, T, P>>: std::formatter<std::string>
{
    using value_type = Lightweight::SqlFixedString<N, T, P>;
    auto format(value_type const& text, format_context& ctx) const -> format_context::iterator
    {
        if constexpr (std::same_as<T, char16_t> || std::same_as<T, char32_t> || std::same_as<T, wchar_t>)
        {
            auto const utf8 = Lightweight::ToUtf8(text.ToStringView());
            auto const stdstring = std::string(reinterpret_cast<char const*>(utf8.data()), utf8.size());
            return std::formatter<std::string>::format(stdstring, ctx);
        }
        else
            return std::formatter<std::string>::format(std::string(text.data(), text.size()), ctx);
    }
};
