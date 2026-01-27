// SPDX-License-Identifier: Apache-2.0

#include "UnicodeConverter.hpp"

#include <array>
#include <codecvt>
#include <cstdint>
#include <locale>

#if defined(_WIN32) || defined(_WIN64)
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

std::u8string ToUtf8(std::u16string_view u16InputString)
{
    std::u8string u8String;
    u8String.reserve(u16InputString.size() * 4);

    char32_t codePoint = 0;
    int codeUnits = 0;
    for (auto const c16: u16InputString)
    {
        if (c16 >= 0xD800 && c16 < 0xDC00)
        {
            codePoint = static_cast<char32_t>((c16 & 0x3FF) << 10);
            codeUnits = 1;
        }
        else if (c16 >= 0xDC00 && c16 < 0xE000)
        {
            codePoint |= c16 & 0x3FF;
            detail::UnicodeConverter<char8_t>::Convert(codePoint + 0x10000, std::back_inserter(u8String));
            codePoint = 0;
            codeUnits = 0;
        }
        else if (codeUnits == 0)
        {
            detail::UnicodeConverter<char8_t>::Convert(static_cast<char32_t>(c16), std::back_inserter(u8String));
        }
        else
        {
            codePoint |= c16 & 0x3FF;
            detail::UnicodeConverter<char8_t>::Convert(codePoint + 0x10000, std::back_inserter(u8String));
            codePoint = 0;
            codeUnits = 0;
        }
    }

    return u8String;
}

// Converts a UTF-8 string to a UTF-16 string.
std::u16string ToUtf16(std::u8string_view u8InputString)
{
    std::u16string u16String;
    u16String.reserve(u8InputString.size());

    char32_t codePoint = 0;
    int codeUnits = 0;
    for (auto const c8: u8InputString)
    {
        if ((c8 & 0b1100'0000) == 0b1000'0000)
        {
            codePoint = (codePoint << 6) | (c8 & 0b0011'1111);
            --codeUnits;
            if (codeUnits == 0)
            {
                detail::UnicodeConverter<char16_t>::Convert(codePoint, std::back_inserter(u16String));
                codePoint = 0;
            }
        }
        else if ((c8 & 0b1000'0000) == 0)
        {
            u16String.push_back(char16_t(c8));
        }
        else if ((c8 & 0b1110'0000) == 0b1100'0000)
        {
            codePoint = c8 & 0b0001'1111;
            codeUnits = 1;
        }
        else if ((c8 & 0b1111'0000) == 0b1110'0000)
        {
            codePoint = c8 & 0b0000'1111;
            codeUnits = 2;
        }
        else if ((c8 & 0b1111'1000) == 0b1111'0000)
        {
            codePoint = c8 & 0b0000'0111;
            codeUnits = 3;
        }
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
    char const* const kWindows1252ToUtf8[32] = {
        "\xE2\x82\xAC", // 0x80 Euro sign
        "\xEF\xBF\xBD", // 0x81 Undefined (replacement char)
        "\xE2\x80\x9A", // 0x82 Single low-9 quotation mark
        "\xC6\x92",     // 0x83 Latin small letter f with hook
        "\xE2\x80\x9E", // 0x84 Double low-9 quotation mark
        "\xE2\x80\xA6", // 0x85 Horizontal ellipsis
        "\xE2\x80\xA0", // 0x86 Dagger
        "\xE2\x80\xA1", // 0x87 Double dagger
        "\xCB\x86",     // 0x88 Modifier letter circumflex accent
        "\xE2\x80\xB0", // 0x89 Per mille sign
        "\xC5\xA0",     // 0x8A Latin capital letter S with caron
        "\xE2\x80\xB9", // 0x8B Single left-pointing angle quotation mark
        "\xC5\x92",     // 0x8C Latin capital ligature OE
        "\xEF\xBF\xBD", // 0x8D Undefined (replacement char)
        "\xC5\xBD",     // 0x8E Latin capital letter Z with caron
        "\xEF\xBF\xBD", // 0x8F Undefined (replacement char)
        "\xEF\xBF\xBD", // 0x90 Undefined (replacement char)
        "\xE2\x80\x98", // 0x91 Left single quotation mark
        "\xE2\x80\x99", // 0x92 Right single quotation mark
        "\xE2\x80\x9C", // 0x93 Left double quotation mark
        "\xE2\x80\x9D", // 0x94 Right double quotation mark
        "\xE2\x80\xA2", // 0x95 Bullet
        "\xE2\x80\x93", // 0x96 En dash
        "\xE2\x80\x94", // 0x97 Em dash
        "\xCB\x9C",     // 0x98 Small tilde
        "\xE2\x84\xA2", // 0x99 Trade mark sign
        "\xC5\xA1",     // 0x9A Latin small letter s with caron
        "\xE2\x80\xBA", // 0x9B Single right-pointing angle quotation mark
        "\xC5\x93",     // 0x9C Latin small ligature oe
        "\xEF\xBF\xBD", // 0x9D Undefined (replacement char)
        "\xC5\xBE",     // 0x9E Latin small letter z with caron
        "\xC5\xB8",     // 0x9F Latin capital letter Y with diaeresis
    };

} // namespace

std::string ConvertWindows1252ToUtf8(std::string_view input)
{
    std::string output;
    output.reserve(input.size() * 2); // Reserve space for potential UTF-8 expansion

    for (char ch: input)
    {
        auto c = static_cast<unsigned char>(ch);
        if (c < 0x80)
        {
            // ASCII range - direct copy
            output += static_cast<char>(c);
        }
        else if (c >= 0x80 && c <= 0x9F)
        {
            // Windows-1252 special range
            output += kWindows1252ToUtf8[c - 0x80];
        }
        else
        {
            // Latin-1 supplement (0xA0-0xFF) - convert to UTF-8
            output += static_cast<char>(0xC0 | (c >> 6));
            output += static_cast<char>(0x80 | (c & 0x3F));
        }
    }

    return output;
}

} // namespace Lightweight
