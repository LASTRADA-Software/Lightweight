// SPDX-License-Identifier: Apache-2.0

#include "UnicodeConverter.hpp"

#include <array>
#include <codecvt>
#include <cstdint>
#include <locale>

#if defined(_WIN32) || defined(_WIN64)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

namespace Lightweight
{

std::u8string ToUtf8(std::u32string_view u32InputString)
{
    std::u8string u8String;
    u8String.reserve(u32InputString.size() * 4);
    for (auto const c32: u32InputString)
        detail::UnicodeConverter<char8_t>::Convert(c32, std::back_inserter(u8String));
    return u8String;
}

// Converts a UTF-16 string to UTF-8; an unpaired surrogate becomes U+FFFD (see Utf16Decoder::Decode()).
std::u8string ToUtf8(std::u16string_view u16InputString)
{
    std::u8string u8String;
    u8String.reserve(u16InputString.size() * 4);

    while (!u16InputString.empty())
    {
        if (u16InputString.front() < 0x80) // ASCII: one code unit, one byte
        {
            u8String.push_back(static_cast<char8_t>(u16InputString.front()));
            u16InputString.remove_prefix(1);
            continue;
        }
        auto const [codePoint, length] = detail::Utf16Decoder::Decode(u16InputString);
        detail::UnicodeConverter<char8_t>::Convert(codePoint, std::back_inserter(u8String));
        u16InputString.remove_prefix(length);
    }

    return u8String;
}

// Converts a UTF-8 string to a UTF-16 string; an ill-formed subsequence becomes one U+FFFD for its maximal subpart (see
// Utf32Converter::Decode()).
std::u16string ToUtf16(std::u8string_view u8InputString)
{
    std::u16string u16String;
    u16String.reserve(u8InputString.size());

    while (!u8InputString.empty())
    {
        if (u8InputString.front() < 0x80) // ASCII: one byte, one code unit
        {
            u16String.push_back(static_cast<char16_t>(u8InputString.front()));
            u8InputString.remove_prefix(1);
            continue;
        }
        auto const [codePoint, length] = detail::Utf32Converter::Decode(u8InputString);
        detail::UnicodeConverter<char16_t>::Convert(codePoint, std::back_inserter(u16String));
        u8InputString.remove_prefix(length);
    }

    return u16String;
}

std::u16string ToUtf16(std::string const& localeInputString)
{
#if defined(_WIN32) || defined(_WIN64)
    std::wstring wideString;
    wideString.resize(
        MultiByteToWideChar(CP_ACP, 0, localeInputString.data(), static_cast<int>(localeInputString.size()), nullptr, 0));
    MultiByteToWideChar(CP_ACP,
                        0,
                        localeInputString.data(),
                        static_cast<int>(localeInputString.size()),
                        wideString.data(),
                        static_cast<int>(wideString.size()));
    return { reinterpret_cast<char16_t const*>(wideString.data()),
             reinterpret_cast<char16_t const*>(wideString.data() + wideString.size()) };
#else
    return ToUtf16(
        std::u8string_view { reinterpret_cast<char8_t const*>(localeInputString.data()), localeInputString.size() });
#endif
}

std::string ToLocaleString(std::u16string_view u16InputString)
{
#if defined(_WIN32) || defined(_WIN64)
    auto const* const wideInput = reinterpret_cast<wchar_t const*>(u16InputString.data());
    auto const wideLength = static_cast<int>(u16InputString.size());
    // Best fit would turn an unrepresentable character into a look-alike (U+221E into "8"); the UTF-8 code page takes no
    // flags.
    DWORD const flags = GetACP() == CP_UTF8 ? 0 : WC_NO_BEST_FIT_CHARS;
    std::string localeString;
    localeString.resize(
        static_cast<size_t>(WideCharToMultiByte(CP_ACP, flags, wideInput, wideLength, nullptr, 0, nullptr, nullptr)));
    WideCharToMultiByte(
        CP_ACP, flags, wideInput, wideLength, localeString.data(), static_cast<int>(localeString.size()), nullptr, nullptr);
    return localeString;
#else
    auto const u8String = ToUtf8(u16InputString);
    return { reinterpret_cast<char const*>(u8String.data()), u8String.size() };
#endif
}

std::wstring ToStdWideString(std::u8string_view u8InputString)
{
    if constexpr (sizeof(wchar_t) == 2)
    {
        // wchar_t is UTF-16 (Windows)
        auto const u16String = ToUtf16(u8InputString);
        return { u16String.data(), u16String.data() + u16String.size() };
    }
    else
    {
        // wchar_t is UTF-32 (any non-Windows platform)
        auto const u32String = ToUtf32(u8InputString);
        return { u32String.begin(), u32String.end() };
    }
}

std::wstring ToStdWideString(std::string const& localeInputString)
{
    // convert from system locale to wchar_t-based wide string
#if defined(_WIN32) || defined(_WIN64)
    std::wstring wideString;
    wideString.resize(
        MultiByteToWideChar(CP_ACP, 0, localeInputString.data(), static_cast<int>(localeInputString.size()), nullptr, 0));
    MultiByteToWideChar(CP_ACP,
                        0,
                        localeInputString.data(),
                        static_cast<int>(localeInputString.size()),
                        wideString.data(),
                        static_cast<int>(wideString.size()));
    return wideString;
#else
    // Get the system locale.
    std::wstring wideString;
    wideString.reserve(localeInputString.size());

    // Convert each character to wide character
    for (char ch: localeInputString)
    {
        wideString.push_back(static_cast<wchar_t>(ch));
    }

    return wideString;
#endif
}

namespace
{

    // Windows-1252 to UTF-8 conversion table for characters 0x80-0x9F
    // These are the special characters in Windows-1252 that differ from Latin-1
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    char8_t const* const kWindows1252ToUtf8[32] = {
        u8"\u20AC", // 0x80 Euro sign
        u8"\uFFFD", // 0x81 Undefined (replacement char)
        u8"\u201A", // 0x82 Single low-9 quotation mark
        u8"\u0192", // 0x83 Latin small letter f with hook
        u8"\u201E", // 0x84 Double low-9 quotation mark
        u8"\u2026", // 0x85 Horizontal ellipsis
        u8"\u2020", // 0x86 Dagger
        u8"\u2021", // 0x87 Double dagger
        u8"\u02C6", // 0x88 Modifier letter circumflex accent
        u8"\u2030", // 0x89 Per mille sign
        u8"\u0160", // 0x8A Latin capital letter S with caron
        u8"\u2039", // 0x8B Single left-pointing angle quotation mark
        u8"\u0152", // 0x8C Latin capital ligature OE
        u8"\uFFFD", // 0x8D Undefined (replacement char)
        u8"\u017D", // 0x8E Latin capital letter Z with caron
        u8"\uFFFD", // 0x8F Undefined (replacement char)
        u8"\uFFFD", // 0x90 Undefined (replacement char)
        u8"\u2018", // 0x91 Left single quotation mark
        u8"\u2019", // 0x92 Right single quotation mark
        u8"\u201C", // 0x93 Left double quotation mark
        u8"\u201D", // 0x94 Right double quotation mark
        u8"\u2022", // 0x95 Bullet
        u8"\u2013", // 0x96 En dash
        u8"\u2014", // 0x97 Em dash
        u8"\u02DC", // 0x98 Small tilde
        u8"\u2122", // 0x99 Trade mark sign
        u8"\u0161", // 0x9A Latin small letter s with caron
        u8"\u203A", // 0x9B Single right-pointing angle quotation mark
        u8"\u0153", // 0x9C Latin small ligature oe
        u8"\uFFFD", // 0x9D Undefined (replacement char)
        u8"\u017E", // 0x9E Latin small letter z with caron
        u8"\u0178", // 0x9F Latin capital letter Y with diaeresis
    };

} // namespace

std::u8string ConvertWindows1252ToUtf8(std::string_view input)
{
    std::u8string output;
    output.reserve(input.size() * 2); // Reserve space for potential UTF-8 expansion

    for (char ch: input)
    {
        auto c = static_cast<unsigned char>(ch);
        if (c < 0x80)
        {
            // ASCII range - direct copy
            output += static_cast<char8_t>(c);
        }
        else if (c >= 0x80 && c <= 0x9F)
        {
            // Windows-1252 special range
            output += kWindows1252ToUtf8[c - 0x80];
        }
        else
        {
            // Latin-1 supplement (0xA0-0xFF) - convert to UTF-8
            output += static_cast<char8_t>(0xC0 | (c >> 6));
            output += static_cast<char8_t>(0x80 | (c & 0x3F));
        }
    }

    return output;
}

} // namespace Lightweight
