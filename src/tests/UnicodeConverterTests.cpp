// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/DataBinder/UnicodeConverter.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace std::string_view_literals;
using namespace Lightweight;

TEST_CASE("UTF-32 to UTF-16 conversion", "[Unicode]")
{
    // U+1F600 -> 0xD83D 0xDE00 (UTF-16)
    auto const u16String = ToUtf16(U"A\U0001F600]"sv);
    REQUIRE(u16String.size() == 4);
    CHECK(u16String[0] == U'A');
    CHECK(u16String[1] == 0xD83D);
    CHECK(u16String[2] == 0xDE00);
    CHECK(u16String[3] == U']');
}

TEST_CASE("UTF-32 to UTF-8 conversion", "[Unicode]")
{
    // U+1F600 -> 0xF0 0x9F 0x98 0x80 (UTF-8)
    auto const u8String = ToUtf8(U"A\U0001F600]"sv);
    REQUIRE(u8String.size() == 6);
    CHECK(u8String[0] == 'A');
    CHECK(u8String[1] == 0xF0);
    CHECK(u8String[2] == 0x9F);
    CHECK(u8String[3] == 0x98);
    CHECK(u8String[4] == 0x80);
    CHECK(u8String[5] == ']');
}

TEST_CASE("UTF-16 to UTF-8 conversion", "[Unicode]")
{
    // U+1F600 -> 0xF0 0x9F 0x98 0x80 (UTF-8)
    auto constexpr u16String = u"A\U0001F600]"sv;
    auto const u8String = ToUtf8(u16String);
    REQUIRE(u8String.size() == 6);
    CHECK(u8String[0] == 'A');
    CHECK(u8String[1] == 0xF0);
    CHECK(u8String[2] == 0x9F);
    CHECK(u8String[3] == 0x98);
    CHECK(u8String[4] == 0x80);
    CHECK(u8String[5] == ']');
}

TEST_CASE("UTF-8 to UTF-16 conversion", "[Unicode]")
{
    // U+1F600 -> 0xD83D 0xDE00 (UTF-16)
    auto const u16String = ToUtf16(u8"A\U0001F600]"sv);
    REQUIRE(u16String.size() == 4);
    CHECK(u16String[0] == u'A');
    CHECK(u16String[1] == 0xD83D);
    CHECK(u16String[2] == 0xDE00);
    CHECK(u16String[3] == u']');
}

TEST_CASE("UTF-8 to UTF-32 conversion", "[Unicode]")
{
    auto const u32String = ToUtf32(u8"A\U0001F600]"sv);
    CHECK(u32String.size() == 3);
    CHECK((unsigned) u32String.at(0) == 'A');
    CHECK((unsigned) u32String.at(1) == 0x1F600);
    CHECK((unsigned) u32String.at(2) == ']');
}

TEST_CASE("UTF-8 to std::wstring conversion", "[Unicode]")
{
    auto const wideString = ToStdWideString(u8"A\U0001F600]"sv);

#if defined(_WIN32) || defined(_WIN64)
    CHECK(wideString.size() == 4);
    CHECK(wideString.at(0) == L'A');
    CHECK(wideString.at(1) == 0xD83D);
    CHECK(wideString.at(2) == 0xDE00);
    CHECK(wideString.at(3) == L']');
#else
    CHECK(wideString.size() == 3);
    CHECK((unsigned) wideString.at(0) == L'A');
    CHECK((unsigned) wideString.at(1) == 0x1F600);
    CHECK((unsigned) wideString.at(2) == L']');
#endif
}
