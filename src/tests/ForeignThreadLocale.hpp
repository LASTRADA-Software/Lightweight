// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) || defined(_WIN64)

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <string>
    #include <string_view>

    #include <Windows.h>

namespace Lightweight::Tests
{

/// Converts @p text to the process ANSI code page, the encoding narrow strings carry.
inline std::string ToProcessCodePage(std::u16string_view text)
{
    auto const* const wideText = reinterpret_cast<wchar_t const*>(text.data());
    auto const wideLength = static_cast<int>(text.size());
    auto result = std::string(
        static_cast<size_t>(WideCharToMultiByte(CP_ACP, 0, wideText, wideLength, nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_ACP, 0, wideText, wideLength, result.data(), static_cast<int>(result.size()), nullptr, nullptr);
    return result;
}

/// Sets a thread locale whose ANSI code page differs from the process one, and restores the previous on destruction.
class ScopedForeignThreadLocale
{
  public:
    ScopedForeignThreadLocale():
        _previousLocale { GetThreadLocale() }
    {
        constexpr auto Cyrillic = MAKELCID(MAKELANGID(LANG_RUSSIAN, SUBLANG_RUSSIAN_RUSSIA), SORT_DEFAULT);
        constexpr auto Western = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);
        constexpr auto CyrillicCodePage = 1251U;
        SetThreadLocale(GetACP() == CyrillicCodePage ? Western : Cyrillic);
    }

    ScopedForeignThreadLocale(ScopedForeignThreadLocale const&) = delete;
    ScopedForeignThreadLocale(ScopedForeignThreadLocale&&) = delete;
    ScopedForeignThreadLocale& operator=(ScopedForeignThreadLocale const&) = delete;
    ScopedForeignThreadLocale& operator=(ScopedForeignThreadLocale&&) = delete;

    ~ScopedForeignThreadLocale()
    {
        SetThreadLocale(_previousLocale);
    }

  private:
    LCID _previousLocale;
};

} // namespace Lightweight::Tests

#endif
