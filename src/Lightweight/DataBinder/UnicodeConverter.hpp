// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"

#include <concepts>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace detail
{

template <typename>
struct UnicodeConverter;

template <>
struct LIGHTWEIGHT_API UnicodeConverter<char8_t>
{
    // Converts a UTF-32 code point to one to four UTF-8 code units.
    template <typename OutputIterator>
    static constexpr OutputIterator Convert(char32_t input, OutputIterator output) noexcept
    {
        if (input <= 0x7F)
        {
            *output++ = static_cast<char8_t>(input & 0b0111'1111);
        }
        else if (input <= 0x07FF)
        {
            *output++ = static_cast<char8_t>(((input >> 6) & 0b0001'1111) | 0b1100'0000);
            *output++ = static_cast<char8_t>(((input >> 0) & 0b0011'1111) | 0b1000'0000);
        }
        else if (input <= 0xFFFF)
        {
            *output++ = static_cast<char8_t>(((input >> 12) & 0b0000'1111) | 0b1110'0000);
            *output++ = static_cast<char8_t>(((input >> 6) & 0b0011'1111) | 0b1000'0000);
            *output++ = static_cast<char8_t>(((input >> 0) & 0b0011'1111) | 0b1000'0000);
        }
        else
        {
            *output++ = static_cast<char8_t>(((input >> 18) & 0b0000'0111) | 0b1111'0000);
            *output++ = static_cast<char8_t>(((input >> 12) & 0b0011'1111) | 0b1000'0000);
            *output++ = static_cast<char8_t>(((input >> 6) & 0b0011'1111) | 0b1000'0000);
            *output++ = static_cast<char8_t>(((input >> 0) & 0b0011'1111) | 0b1000'0000);
        }
        return output;
    }
};

template <>
struct LIGHTWEIGHT_API UnicodeConverter<char16_t>
{
    // Converts a UTF-32 code point to one or two UTF-16 code units.
    template <typename OutputIterator>
    static constexpr OutputIterator Convert(char32_t input, OutputIterator output) noexcept
    {
        if (input < 0xD800) // [0x0000 .. 0xD7FF]
        {
            *output++ = char16_t(input);
            return output;
        }
        else if (input < 0x10000)
        {
            if (input < 0xE000)
                return output; // The UTF-16 code point can not be in surrogate range.

            // [0xE000 .. 0xFFFF]
            *output++ = char16_t(input);
            return output;
        }
        else if (input < 0x110000) // [0xD800 .. 0xDBFF] [0xDC00 .. 0xDFFF]
        {
            *output++ = char16_t(0xD7C0 + (input >> 10));
            *output++ = char16_t(0xDC00 + (input & 0x3FF));
            return output;
        }
        else
            return output; // Too large UTF-16 code point.
    }
};

struct Utf32Converter
{
    char32_t codePoint = 0;
    int codeUnits = 0;

    static constexpr auto InvalidCodePoint = char32_t { 0xFFFD };

    constexpr std::optional<char32_t> Process(char8_t c8) noexcept
    {
        if ((c8 & 0b1100'0000) == 0b1000'0000)
        {
            if (codeUnits == 0)
                return InvalidCodePoint;
            codePoint <<= 6;
            codePoint |= c8 & 0b0011'1111;
            if (--codeUnits == 0)
            {
                auto result = codePoint;
                codePoint = 0;
                return result;
            }
            return std::nullopt;
        }
        if (codeUnits == 0)
        {
            if ((c8 & 0b1000'0000) == 0)
                return c8;
            if ((c8 & 0b1110'0000) == 0b1100'0000)
            {
                codePoint = c8 & 0b0001'1111;
                codeUnits = 1;
                return std::nullopt;
            }
            if ((c8 & 0b1111'0000) == 0b1110'0000)
            {
                codePoint = c8 & 0b0000'1111;
                codeUnits = 2;
                return std::nullopt;
            }
            if ((c8 & 0b1111'1000) == 0b1111'0000)
            {
                codePoint = c8 & 0b0000'0111;
                codeUnits = 3;
                return std::nullopt;
            }
            return InvalidCodePoint;
        }
        return InvalidCodePoint;
    }
};

struct [[nodiscard]] Utf32Iterator
{
    std::u8string_view u8InputString;

    struct [[nodiscard]] iterator
    {
        std::u8string_view::iterator current {};
        std::u8string_view::iterator end {};
        char32_t codePoint = Utf32Converter::InvalidCodePoint;

        constexpr explicit iterator(std::u8string_view::iterator current, std::u8string_view::iterator end) noexcept:
            current { current },
            end { end }
        {
            if (current != end)
                operator++();
        }

        constexpr char32_t operator*() const noexcept
        {
            return codePoint;
        }

        constexpr iterator& operator++() noexcept
        {
            auto converter = Utf32Converter {};
            codePoint = Utf32Converter::InvalidCodePoint;
            while (current != end)
            {
                if (auto const result = converter.Process(*current++); result.has_value())
                {
                    codePoint = *result;
                    break;
                }
            }
            return *this;
        }

        constexpr iterator& operator++(int) noexcept
        {
            return ++*this;
        }

        constexpr bool operator==(iterator const& other) const noexcept
        {
            return current == other.current && codePoint == other.codePoint;
        }

        constexpr bool operator!=(iterator const& other) const noexcept
        {
            return !(*this == other);
        }
    };

    iterator begin() const noexcept
    {
        return iterator { u8InputString.begin(), u8InputString.end() };
    }

    iterator end() const noexcept
    {
        return iterator { u8InputString.end(), u8InputString.end() };
    }
};

} // namespace detail

/// @defgroup Unicode Unicode conversion functions
///
/// @brief Functions for converting between different (Unicode) encodings.

/// Converts from UTF-32 to UTF-8.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::u8string ToUtf8(std::u32string_view u32InputString);

/// Converts from UTF-16 to UTF-8.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::u8string ToUtf8(std::u16string_view u16InputString);

/// Converts from UTF-16 (as wchar_t) to UTF-8.
///
/// @ingroup Unicode
template <typename T>
    requires(std::same_as<T, wchar_t> && sizeof(wchar_t) == 2)
inline LIGHTWEIGHT_FORCE_INLINE std::u8string ToUtf8(std::basic_string_view<T> u16InputString)
{
    return ToUtf8(std::u16string_view(reinterpret_cast<char16_t const*>(u16InputString.data()), u16InputString.size()));
}

/// Converts a wchar_t-based wide string view to UTF-8.
///
/// @ingroup Unicode
template <typename T>
    requires(std::same_as<T, wchar_t> && sizeof(wchar_t) == 4)
inline LIGHTWEIGHT_FORCE_INLINE std::u8string ToUtf8(std::basic_string_view<T> u32InputString)
{
    return ToUtf8(std::u32string_view(reinterpret_cast<char32_t const*>(u32InputString.data()), u32InputString.size()));
}

/// Converts from UTF-32 to UTF-16.
///
/// @ingroup Unicode
template <typename T>
    requires std::same_as<T, char32_t> || (std::same_as<T, wchar_t> && sizeof(wchar_t) == 4)
std::u16string ToUtf16(std::basic_string_view<T> const u32InputString)
{
    std::u16string u16OutputString;
    u16OutputString.reserve(u32InputString.size());
    detail::UnicodeConverter<char16_t> converter;
    for (auto const c: u32InputString)
        converter.Convert(c, std::back_inserter(u16OutputString));
    return u16OutputString;
}

/// Converts from UTF-8 to UTF-16.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::u16string ToUtf16(std::u8string_view u8InputString);

/// Converts from local 8-bit string to UTF-16.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::u16string ToUtf16(std::string const& localeInputString);

/// Converts from UTF-8 to UTF-32.
///
/// @ingroup Unicode
template <typename T = std::u32string>
T ToUtf32(std::u8string_view u8InputString)
{
    auto result = T {};
    for (char32_t const c32: detail::Utf32Iterator { u8InputString })
        result.push_back(c32);
    return result;
}

/// Converts from UTF-16 to UTF-32.
///
/// @ingroup Unicode
template <typename T = std::u32string>
T ToUtf32(std::u16string_view u16InputString)
{
    auto result = T {};

    for (char16_t const c16: u16InputString)
    {
        if (c16 < 0xD800 || c16 >= 0xDC00)
            result.push_back(c16);
        else
            result.push_back(0x10000 + ((c16 & 0x3FF) | ((c16 & 0x3FF) << 10)));
    }

    return result;
}

// Converts a UTF-8 string to wchar_t-based wide string.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::wstring ToStdWideString(std::u8string_view u8InputString);

// Converts a local 8-bit string to wchar_t-based wide string.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::wstring ToStdWideString(std::string const& localeInputString);
