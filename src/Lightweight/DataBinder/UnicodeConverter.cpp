#include "UnicodeConverter.hpp"

#include <codecvt>
#include <locale>

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h>
#endif

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
            codePoint = (c16 & 0x3FF) << 10;
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
            detail::UnicodeConverter<char8_t>::Convert(c16, std::back_inserter(u8String));
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
    wideString.resize(MultiByteToWideChar(
        CP_ACP, 0, localeInputString.data(), static_cast<int>(localeInputString.size()), nullptr, 0));
    MultiByteToWideChar(CP_ACP,
                        0,
                        localeInputString.data(),
                        static_cast<int>(localeInputString.size()),
                        wideString.data(),
                        static_cast<int>(wideString.size()));
    return { reinterpret_cast<char16_t const*>(wideString.data()),
             reinterpret_cast<char16_t const*>(wideString.data() + wideString.size()) };
#else
    std::locale sys_locale("");
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> utf16conv;
    auto const result = utf16conv.from_bytes(localeInputString);
    return { reinterpret_cast<char16_t const*>(result.data()),
             reinterpret_cast<char16_t const*>(result.data() + result.size()) };
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
    wideString.resize(MultiByteToWideChar(
        CP_ACP, 0, localeInputString.data(), static_cast<int>(localeInputString.size()), nullptr, 0));
    MultiByteToWideChar(CP_ACP,
                        0,
                        localeInputString.data(),
                        static_cast<int>(localeInputString.size()),
                        wideString.data(),
                        static_cast<int>(wideString.size()));
    return wideString;
#else
    // Get the system locale.
    std::locale sys_locale("");
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> utf16conv;
    return utf16conv.from_bytes(localeInputString);
#endif
}
