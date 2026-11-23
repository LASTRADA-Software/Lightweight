// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Api.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

namespace Lightweight
{

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

    /// Decodes UTF-8 one code point at a time (Unicode 15, table 3-7, "well-formed UTF-8 byte sequences").
    ///
    /// An ill-formed sequence gives one U+FFFD for its maximal subpart, Unicode's recommended practice (section
    /// 3.9, "U+FFFD Substitution of Maximal Subparts") and what WHATWG's decoder produces (MultiByteToWideChar(CP_UTF8)
    /// agrees on truncated sequences and stray bytes, but takes a lead byte and an out-of-range second byte as one):
    /// a lead byte and the continuation bytes that can still begin a well-formed sequence after it (E2 82 41
    /// gives U+FFFD A), or a single byte that begins none (an overlong lead, an encoded surrogate's second byte,
    /// a stray continuation byte). Decoding resumes at the byte that ended the subpart, so no valid byte is lost.
    struct Utf32Converter
    {
        /// The code point an ill-formed byte decodes to.
        static constexpr auto InvalidCodePoint = char32_t { 0xFFFD };

        /// One decoded code point and the bytes it took.
        struct Decoded
        {
            char32_t codePoint; //!< The code point, or InvalidCodePoint.
            std::size_t length; //!< Bytes consumed: the sequence's length, or the maximal subpart's if ill-formed.
        };

        /// The well-formed multi-byte sequences, by lead byte: how many bytes they take, and the range of the
        /// second byte, which rules out overlong encodings, surrogates and code points above U+10FFFF.
        struct SequenceRule
        {
            char8_t firstLead;   //!< The first lead byte the rule covers.
            char8_t lastLead;    //!< The last lead byte the rule covers.
            std::uint8_t length; //!< The sequence's length in bytes, lead byte included.
            char8_t secondMin;   //!< The smallest second byte of a well-formed sequence.
            char8_t secondMax;   //!< The largest second byte of a well-formed sequence.
        };

        static constexpr auto SequenceRules = std::array {
            SequenceRule { .firstLead = 0xC2, .lastLead = 0xDF, .length = 2, .secondMin = 0x80, .secondMax = 0xBF },
            SequenceRule { .firstLead = 0xE0, .lastLead = 0xE0, .length = 3, .secondMin = 0xA0, .secondMax = 0xBF },
            SequenceRule { .firstLead = 0xE1, .lastLead = 0xEC, .length = 3, .secondMin = 0x80, .secondMax = 0xBF },
            SequenceRule { .firstLead = 0xED, .lastLead = 0xED, .length = 3, .secondMin = 0x80, .secondMax = 0x9F },
            SequenceRule { .firstLead = 0xEE, .lastLead = 0xEF, .length = 3, .secondMin = 0x80, .secondMax = 0xBF },
            SequenceRule { .firstLead = 0xF0, .lastLead = 0xF0, .length = 4, .secondMin = 0x90, .secondMax = 0xBF },
            SequenceRule { .firstLead = 0xF1, .lastLead = 0xF3, .length = 4, .secondMin = 0x80, .secondMax = 0xBF },
            SequenceRule { .firstLead = 0xF4, .lastLead = 0xF4, .length = 4, .secondMin = 0x80, .secondMax = 0x8F },
        };

        /// The rule of the sequences lead byte @p lead begins.
        /// @param lead The lead byte.
        /// @return The rule, or @c nullptr for a byte that begins no well-formed multi-byte sequence.
        static constexpr SequenceRule const* FindSequenceRule(char8_t lead) noexcept
        {
            for (auto const& rule: SequenceRules)
                if (lead >= rule.firstLead && lead <= rule.lastLead)
                    return &rule;
            return nullptr;
        }

        /// Decodes the code point @p text starts with.
        /// @param text The input; not empty.
        /// @return The code point and the bytes it took.
        static constexpr Decoded Decode(std::u8string_view text) noexcept
        {
            auto const lead = text.front();
            if (lead < 0x80)
                return { .codePoint = static_cast<char32_t>(lead), .length = 1 };

            SequenceRule const* const rule = FindSequenceRule(lead);
            // A byte that begins no well-formed sequence, or a lead byte whose second byte cannot follow it, is a
            // maximal subpart of its own.
            if (rule == nullptr || text.size() < 2 || text[1] < rule->secondMin || text[1] > rule->secondMax)
                return { .codePoint = InvalidCodePoint, .length = 1 };

            // The lead byte keeps 7 - length payload bits; each continuation byte six. Past the second byte, any
            // continuation byte can follow; the first that is missing or is none ends the subpart before it.
            auto codePoint = static_cast<char32_t>(lead & (0b0111'1111 >> rule->length));
            for (auto const index: std::views::iota(std::size_t { 1 }, std::size_t { rule->length }))
            {
                if (index >= text.size() || (text[index] & 0b1100'0000) != 0b1000'0000)
                    return { .codePoint = InvalidCodePoint, .length = index };
                codePoint = (codePoint << 6) | (text[index] & 0b0011'1111);
            }
            return { .codePoint = codePoint, .length = rule->length };
        }
    };

    /// The code points of a UTF-8 string (see Utf32Converter::Decode()).
    struct [[nodiscard]] Utf32Iterator
    {
        std::u8string_view u8InputString;

        struct [[nodiscard]] iterator
        {
            std::u8string_view rest {}; //!< The input from the current code point on.

            constexpr char32_t operator*() const noexcept
            {
                return Utf32Converter::Decode(rest).codePoint;
            }

            constexpr iterator& operator++() noexcept
            {
                rest.remove_prefix(Utf32Converter::Decode(rest).length);
                return *this;
            }

            constexpr iterator operator++(int) noexcept
            {
                auto const previous = *this;
                ++*this;
                return previous;
            }

            /// Positions compare equal: the end is where the input ends, whatever was decoded last.
            constexpr bool operator==(iterator const& other) const noexcept
            {
                return rest.data() == other.rest.data();
            }
        };

        iterator begin() const noexcept
        {
            return iterator { .rest = u8InputString };
        }

        iterator end() const noexcept
        {
            return iterator { .rest = u8InputString.substr(u8InputString.size()) };
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
    for (auto const c: u32InputString)
        detail::UnicodeConverter<char16_t>::Convert(static_cast<char32_t>(c), std::back_inserter(u16OutputString));
    return u16OutputString;
}

/// Converts a wchar_t-based wide string view to UTF-16.
///
/// When `sizeof(wchar_t) == 2` (typically Windows), the wide string is already UTF-16,
/// so this is a reinterpreting copy.
///
/// @ingroup Unicode
template <typename T>
    requires(std::same_as<T, wchar_t> && sizeof(wchar_t) == 2)
inline LIGHTWEIGHT_FORCE_INLINE std::u16string ToUtf16(std::basic_string_view<T> u16InputString)
{
    return { reinterpret_cast<char16_t const*>(u16InputString.data()), u16InputString.size() };
}

/// Converts from UTF-8 to UTF-16. Each ill-formed subsequence becomes one U+FFFD for its maximal subpart, as
/// Unicode recommends and WHATWG does (E2 82 41 gives U+FFFD A); see
/// @c Utf32Converter::Decode in the source.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::u16string ToUtf16(std::u8string_view u8InputString);

/// Converts from local 8-bit string to UTF-16.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::u16string ToUtf16(std::string const& localeInputString);

/// Converts from UTF-16 to a local 8-bit string, the inverse of @c ToUtf16(std::string const&).
///
/// On Windows the result is in the process ANSI code page; characters it cannot represent become its
/// default character (usually @c ?), never a best-fit look-alike. Elsewhere the result is UTF-8.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::string ToLocaleString(std::u16string_view u16InputString);

/// Converts from UTF-8 to UTF-32. Each ill-formed subsequence becomes one U+FFFD for its maximal subpart, as
/// Unicode recommends and WHATWG does (E2 82 41 gives U+FFFD A).
///
/// @ingroup Unicode
template <typename T = std::u32string>
T ToUtf32(std::u8string_view u8InputString)
{
    auto result = T {};
    result.reserve(u8InputString.size());
    while (!u8InputString.empty())
    {
        if (u8InputString.front() < 0x80) // ASCII: one byte, one code point
        {
            result.push_back(static_cast<T::value_type>(u8InputString.front()));
            u8InputString.remove_prefix(1);
            continue;
        }
        auto const [codePoint, length] = detail::Utf32Converter::Decode(u8InputString);
        result.push_back(static_cast<T::value_type>(codePoint));
        u8InputString.remove_prefix(length);
    }
    return result;
}

/// Converts from UTF-16 to UTF-32.
///
/// @ingroup Unicode
template <typename T = std::u32string>
T ToUtf32(std::u16string_view u16InputString)
{
    auto result = T {};
    result.reserve(u16InputString.size());

    for (size_t i = 0; i < u16InputString.size(); ++i)
    {
        auto const c16 = u16InputString[i];
        if (c16 >= 0xD800 && c16 <= 0xDBFF && i + 1 < u16InputString.size() && u16InputString[i + 1] >= 0xDC00
            && u16InputString[i + 1] <= 0xDFFF)
        {
            auto const high = static_cast<char32_t>(c16 - 0xD800);
            auto const low = static_cast<char32_t>(u16InputString[++i] - 0xDC00);
            result.push_back(static_cast<char32_t>(0x10000 + (high << 10) + low));
        }
        else if (c16 >= 0xD800 && c16 <= 0xDFFF)
        {
            // Orphan surrogate (high without low, or stray low) — emit replacement.
            result.push_back(detail::Utf32Converter::InvalidCodePoint);
        }
        else
        {
            result.push_back(static_cast<char32_t>(c16));
        }
    }

    return result;
}

/// Converts a UTF-8 string to wchar_t-based wide string.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::wstring ToStdWideString(std::u8string_view u8InputString);

/// Converts a local 8-bit string to wchar_t-based wide string.
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::wstring ToStdWideString(std::string const& localeInputString);

/// Converts from Windows-1252 encoding to UTF-8.
///
/// @param input The Windows-1252 encoded string
/// @return UTF-8 encoded string
///
/// @ingroup Unicode
LIGHTWEIGHT_API std::u8string ConvertWindows1252ToUtf8(std::string_view input);

} // namespace Lightweight
