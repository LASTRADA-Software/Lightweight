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

// =============================================================================
// Branch coverage for the Unicode converter primitives.
// =============================================================================

TEST_CASE("UTF-8 encoding: 1-byte ASCII", "[Unicode]")
{
    // ASCII path of UnicodeConverter<char8_t>::Convert: input <= 0x7F.
    auto const u8String = ToUtf8(U""sv); // DEL, the highest single-byte UTF-8 codepoint.
    REQUIRE(u8String.size() == 1);
    CHECK(u8String[0] == 0x7F);
}

TEST_CASE("UTF-8 encoding: 2-byte sequence", "[Unicode]")
{
    // Two-byte path: 0x80 <= input <= 0x7FF (e.g. Latin-1 supplement letters).
    auto const u8String = ToUtf8(U"ä"sv); // ä = U+00E4 -> 0xC3 0xA4
    REQUIRE(u8String.size() == 2);
    CHECK(u8String[0] == 0xC3);
    CHECK(u8String[1] == 0xA4);
}

TEST_CASE("UTF-8 encoding: 3-byte sequence (BMP)", "[Unicode]")
{
    // Three-byte path: 0x800 <= input <= 0xFFFF (BMP characters above 0x7FF).
    // U+4E2D (中) = 0xE4 0xB8 0xAD; U+20AC (€) = 0xE2 0x82 0xAC.
    auto const u8String = ToUtf8(U"中"sv);
    REQUIRE(u8String.size() == 3);
    CHECK(u8String[0] == 0xE4);
    CHECK(u8String[1] == 0xB8);
    CHECK(u8String[2] == 0xAD);

    auto const euroU8 = ToUtf8(U"€"sv);
    REQUIRE(euroU8.size() == 3);
    CHECK(euroU8[0] == 0xE2);
    CHECK(euroU8[1] == 0x82);
    CHECK(euroU8[2] == 0xAC);
}

TEST_CASE("UTF-8 encoding: 4-byte sequence (supplementary plane)", "[Unicode]")
{
    // Four-byte path: input > 0xFFFF — already covered by the emoji test above
    // but pinned here for symmetry with the others.
    auto const u8String = ToUtf8(U"\U0001F4A1"sv); // 💡 = U+1F4A1 -> 0xF0 0x9F 0x92 0xA1
    REQUIRE(u8String.size() == 4);
    CHECK(u8String[0] == 0xF0);
    CHECK(u8String[1] == 0x9F);
    CHECK(u8String[2] == 0x92);
    CHECK(u8String[3] == 0xA1);
}

TEST_CASE("UTF-16 encoding: BMP code points map 1:1", "[Unicode]")
{
    // UnicodeConverter<char16_t>::Convert: input < 0xD800 — direct copy.
    auto const u16Low = ToUtf16(U"Aä中"sv);
    REQUIRE(u16Low.size() == 3);
    CHECK(u16Low[0] == u'A');
    CHECK(u16Low[1] == 0x00E4);
    CHECK(u16Low[2] == 0x4E2D);
}

TEST_CASE("UTF-16 encoding: BMP non-surrogate above 0xDFFF", "[Unicode]")
{
    // Path: 0xE000 <= input < 0x10000 — direct copy of one char16_t.
    auto const u16High = ToUtf16(U"�"sv);
    REQUIRE(u16High.size() == 2);
    CHECK(u16High[0] == 0xE000);
    CHECK(u16High[1] == 0xFFFD);
}

TEST_CASE("UTF-16 encoding: surrogate-range input is dropped", "[Unicode]")
{
    // Path: 0xD800 <= input < 0xE000 (the surrogate range) is invalid in a
    // UTF-32 source string. UnicodeConverter<char16_t>::Convert drops these
    // silently to avoid producing structurally-broken UTF-16.
    std::u32string const surrogateInput { static_cast<char32_t>(0xD800), U'X' };
    auto const u16 = ToUtf16(std::u32string_view { surrogateInput });
    // Only 'X' should make it through; the orphan surrogate is dropped.
    REQUIRE(u16.size() == 1);
    CHECK(u16[0] == u'X');
}

TEST_CASE("UTF-16 encoding: out-of-range code point is dropped", "[Unicode]")
{
    // Path: input >= 0x110000 — the code point exceeds Unicode's defined range,
    // and UnicodeConverter<char16_t>::Convert drops it silently.
    std::u32string const tooLarge { static_cast<char32_t>(0x110000), U'Y' };
    auto const u16 = ToUtf16(std::u32string_view { tooLarge });
    REQUIRE(u16.size() == 1);
    CHECK(u16[0] == u'Y');
}

TEST_CASE("UTF-32 from UTF-16: orphan surrogate replaced with U+FFFD", "[Unicode]")
{
    // Pin the orphan-surrogate branch in ToUtf32(u16): a high surrogate without
    // a following low surrogate produces U+FFFD (replacement char) in the output.
    std::u16string const orphan { static_cast<char16_t>(0xD83D), u'A' };
    auto const u32 = ToUtf32(std::u16string_view { orphan });
    REQUIRE(u32.size() == 2);
    CHECK(u32[0] == 0xFFFD);
    CHECK(u32[1] == U'A');
}

TEST_CASE("UTF-32 from UTF-16: stray low surrogate replaced with U+FFFD", "[Unicode]")
{
    // A lone low surrogate (without a preceding high) is also invalid.
    std::u16string const stray { static_cast<char16_t>(0xDC00), u'B' };
    auto const u32 = ToUtf32(std::u16string_view { stray });
    REQUIRE(u32.size() == 2);
    CHECK(u32[0] == 0xFFFD);
    CHECK(u32[1] == U'B');
}

TEST_CASE("UTF-32 from UTF-8: lone continuation byte is replaced", "[Unicode]")
{
    // Utf32Converter::Process: continuation byte (0b10xxxxxx) with codeUnits == 0
    // is invalid and emits InvalidCodePoint (U+FFFD).
    std::u8string const malformed { 0x80, u8'A' };
    auto const u32 = ToUtf32(std::u8string_view { malformed });
    // 0x80 → U+FFFD; 'A' → U+0041
    REQUIRE(u32.size() == 2);
    CHECK(u32[0] == 0xFFFD);
    CHECK(u32[1] == U'A');
}

TEST_CASE("UTF-32 from UTF-8: invalid 5-byte lead is replaced", "[Unicode]")
{
    // A byte starting with 0b1111'1xxx is not a valid UTF-8 lead.
    std::u8string const bad { 0xF8, u8'B' };
    auto const u32 = ToUtf32(std::u8string_view { bad });
    REQUIRE(u32.size() == 2);
    CHECK(u32[0] == 0xFFFD);
    CHECK(u32[1] == U'B');
}

TEST_CASE("UTF-32 from UTF-8: 3-byte BMP sequence", "[Unicode]")
{
    // The 3-byte UTF-8 lead path (0b1110xxxx) — exercises the
    // codePoint = c8 & 0b0000'1111 / codeUnits = 2 branch.
    auto const u32 = ToUtf32(u8"中"sv); // 中 = E4 B8 AD
    REQUIRE(u32.size() == 1);
    CHECK(u32[0] == 0x4E2D);
}

TEST_CASE("UTF-32 from UTF-8: 2-byte sequence", "[Unicode]")
{
    // The 2-byte UTF-8 lead path (0b110xxxxx).
    auto const u32 = ToUtf32(u8"ä"sv); // ä = C3 A4
    REQUIRE(u32.size() == 1);
    CHECK(u32[0] == 0x00E4);
}

TEST_CASE("Windows-1252 to UTF-8: ASCII passthrough", "[Unicode]")
{
    auto const out = ConvertWindows1252ToUtf8("Hello, World!"sv);
    auto const view = std::string_view(reinterpret_cast<char const*>(out.data()), out.size());
    CHECK(view == "Hello, World!"sv);
}

TEST_CASE("Windows-1252 to UTF-8: 0x80 special range", "[Unicode]")
{
    // The 0x80..0x9F range maps through a Windows-1252 lookup table to specific
    // Unicode code points. A handful of representative characters:
    // 0x80 -> U+20AC (€), 0x85 -> U+2026 (…), 0x99 -> U+2122 (™).
    std::string const input = { '\x80', '\x85', '\x99' };
    auto const out = ConvertWindows1252ToUtf8(input);
    auto const view = std::string_view(reinterpret_cast<char const*>(out.data()), out.size());
    // €=E2 82 AC, …=E2 80 A6, ™=E2 84 A2 — all 3 bytes each.
    CHECK(view == "\xE2\x82\xAC\xE2\x80\xA6\xE2\x84\xA2"sv);
}

TEST_CASE("Windows-1252 to UTF-8: Latin-1 supplement passthrough", "[Unicode]")
{
    // 0xA0..0xFF: standard Latin-1 supplement, mapped to 2-byte UTF-8 sequences
    // via direct bit manipulation (no lookup table).
    std::string const input = { '\xE4', '\xDF' }; // ä, ß
    auto const out = ConvertWindows1252ToUtf8(input);
    auto const view = std::string_view(reinterpret_cast<char const*>(out.data()), out.size());
    CHECK(view == "\xC3\xA4\xC3\x9F"sv);
}

TEST_CASE("Windows-1252 to UTF-8: undefined slots emit U+FFFD", "[Unicode]")
{
    // 0x81, 0x8D, 0x8F, 0x90, 0x9D are undefined in Windows-1252; the table maps them to U+FFFD.
    std::string const input = { '\x81', '\x8D', '\x8F', '\x90', '\x9D' };
    auto const out = ConvertWindows1252ToUtf8(input);
    auto const view = std::string_view(reinterpret_cast<char const*>(out.data()), out.size());
    // Each U+FFFD is 3 bytes: EF BF BD. Five replacements = 15 bytes.
    REQUIRE(view.size() == 15);
    for (std::size_t i = 0; i < 5; ++i)
    {
        CHECK(static_cast<unsigned char>(view[(i * 3) + 0]) == 0xEF);
        CHECK(static_cast<unsigned char>(view[(i * 3) + 1]) == 0xBF);
        CHECK(static_cast<unsigned char>(view[(i * 3) + 2]) == 0xBD);
    }
}

TEST_CASE("ToUtf16(local 8-bit string)", "[Unicode]")
{
    // Goes through the ToUtf16(std::string const&) overload. On Linux it's
    // equivalent to treating bytes as UTF-8.
    std::string const ascii = "Hello";
    auto const u16 = ToUtf16(ascii);
    REQUIRE(u16.size() == 5);
    CHECK(u16[0] == u'H');
    CHECK(u16[4] == u'o');
}

TEST_CASE("ToStdWideString(local 8-bit string)", "[Unicode]")
{
    // Exercises the ToStdWideString(std::string const&) overload.
    std::string const ascii = "Local";
    auto const wide = ToStdWideString(ascii);
    REQUIRE(wide.size() == 5);
    CHECK(wide[0] == L'L');
    CHECK(wide[4] == L'l');
}

TEST_CASE("UTF-16 to UTF-8: BMP-only payload", "[Unicode]")
{
    // Pin the codeUnits == 0 path of ToUtf8(u16): no surrogates, just BMP chars.
    auto const out = ToUtf8(u"Hello ä中"sv);
    auto const view = std::string_view(reinterpret_cast<char const*>(out.data()), out.size());
    CHECK(view == "Hello \xC3\xA4\xE4\xB8\xAD"sv);
}

TEST_CASE("ToUtf16 from wstring_view", "[Unicode]")
{
    // Exercises the wchar_t-based ToUtf16 overloads (different specialization
    // depending on sizeof(wchar_t)).
    std::wstring const wide = L"Hi";
    auto const u16 = ToUtf16(std::basic_string_view<wchar_t> { wide });
    REQUIRE(u16.size() == 2);
    CHECK(u16[0] == u'H');
    CHECK(u16[1] == u'i');
}

TEST_CASE("ToUtf8 from wstring_view", "[Unicode]")
{
    std::wstring const wide = L"Hi";
    auto const u8 = ToUtf8(std::basic_string_view<wchar_t> { wide });
    auto const view = std::string_view(reinterpret_cast<char const*>(u8.data()), u8.size());
    CHECK(view == "Hi"sv);
}

TEST_CASE("Empty string round-trips through every converter", "[Unicode]")
{
    // Empty inputs hit the early-return / no-iteration path of every loop.
    CHECK(ToUtf8(std::u32string_view {}).empty());
    CHECK(ToUtf8(std::u16string_view {}).empty());
    CHECK(ToUtf16(std::u8string_view {}).empty());
    CHECK(ToUtf32(std::u8string_view {}).empty());
    CHECK(ToUtf32(std::u16string_view {}).empty());
    CHECK(ConvertWindows1252ToUtf8({}).empty());
    CHECK(ToStdWideString(std::u8string_view {}).empty());
    CHECK(ToStdWideString(std::string {}).empty());
    CHECK(ToUtf16(std::string {}).empty());
}
