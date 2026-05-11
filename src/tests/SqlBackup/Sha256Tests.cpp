// SPDX-License-Identifier: Apache-2.0

#include <Lightweight/SqlBackup/Sha256.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using Lightweight::SqlBackup::Sha256;

// SHA-256 reference digests come from FIPS 180-2 / NIST test vectors:
// https://csrc.nist.gov/projects/cryptographic-standards-and-guidelines/example-values
namespace
{
constexpr std::string_view EmptyDigest = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr std::string_view AbcDigest = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr std::string_view TwoBlockDigest = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
} // namespace

TEST_CASE("Sha256: empty input matches NIST reference", "[Sha256]")
{
    CHECK(Sha256::Hash(std::string_view {}) == EmptyDigest);
}

TEST_CASE("Sha256: 'abc' matches NIST reference (single block)", "[Sha256]")
{
    CHECK(Sha256::Hash(std::string_view { "abc" }) == AbcDigest);
}

TEST_CASE("Sha256: 448-bit two-block message matches NIST reference", "[Sha256]")
{
    // Exact length straddles the single-block boundary, exercising the padding path.
    constexpr std::string_view input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(Sha256::Hash(input) == TwoBlockDigest);
}

TEST_CASE("Sha256: one-million-'a' message matches NIST reference", "[Sha256]")
{
    // FIPS 180-2 sample C.3 — verifies the streaming + count path under load.
    std::string const input(1'000'000, 'a');
    CHECK(Sha256::Hash(input) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("Sha256: streaming Update matches one-shot Hash", "[Sha256]")
{
    constexpr std::string_view part1 = "The quick brown fox";
    constexpr std::string_view part2 = " jumps over the lazy dog";

    Sha256 hasher;
    hasher.Update(part1);
    hasher.Update(part2);

    CHECK(Sha256::ToHex(hasher.Finalize()) == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

    std::string const combined = std::string { part1 } + std::string { part2 };
    CHECK(Sha256::Hash(combined) == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST_CASE("Sha256: Reset returns hasher to initial state", "[Sha256]")
{
    Sha256 hasher;
    hasher.Update(std::string_view { "pollute the state" });
    hasher.Reset();

    CHECK(Sha256::ToHex(hasher.Finalize()) == EmptyDigest);
}

TEST_CASE("Sha256: byte-pointer Update and span Update produce identical digests", "[Sha256]")
{
    std::vector<uint8_t> const bytes { 'a', 'b', 'c' };

    Sha256 viaPointer;
    viaPointer.Update(bytes.data(), bytes.size());

    Sha256 viaSpan;
    viaSpan.Update(std::span<uint8_t const> { bytes });

    auto const pointerDigest = Sha256::ToHex(viaPointer.Finalize());
    auto const spanDigest = Sha256::ToHex(viaSpan.Finalize());
    CHECK(pointerDigest == spanDigest);
    CHECK(pointerDigest == AbcDigest);
}

TEST_CASE("Sha256: Hash(data, size) overload matches string_view overload", "[Sha256]")
{
    constexpr std::string_view text = "Lightweight";
    CHECK(Sha256::Hash(text.data(), text.size()) == Sha256::Hash(text));
}

TEST_CASE("Sha256: digest size and hex encoding length are documented constants", "[Sha256]")
{
    STATIC_REQUIRE(Sha256::DigestSize == 32);
    STATIC_REQUIRE(Sha256::BlockSize == 64);

    // 32-byte digest must render to exactly 64 lowercase hex characters.
    CHECK(Sha256::Hash(std::string_view { "abc" }).size() == Sha256::DigestSize * 2);
}

TEST_CASE("Sha256: digests differ when a single bit flips", "[Sha256]")
{
    auto const a = Sha256::Hash(std::string_view { "Lightweight" });
    auto const b = Sha256::Hash(std::string_view { "lightweight" }); // differs by one case bit
    CHECK(a != b);
}

TEST_CASE("Sha256: chunked updates across block boundaries match contiguous hash", "[Sha256]")
{
    // 200 bytes — exceeds the 64-byte block size, so chunked vs. one-shot exercises
    // both the buffered prefix path and the full-block fast path.
    std::string const input(200, 'x');

    Sha256 chunked;
    for (size_t offset = 0; offset < input.size(); offset += 7)
    {
        auto const chunk = input.substr(offset, 7);
        chunked.Update(chunk);
    }

    CHECK(Sha256::ToHex(chunked.Finalize()) == Sha256::Hash(input));
}

TEST_CASE("Sha256: ToHex output is lowercase and zero-padded", "[Sha256]")
{
    // Use a value with leading zero bytes to verify zero-padding behavior.
    std::array<uint8_t, Sha256::DigestSize> digest {};
    digest[0] = 0x00;
    digest[1] = 0x0f;
    digest[Sha256::DigestSize - 1] = 0xab;

    auto const hex = Sha256::ToHex(digest);
    REQUIRE(hex.size() == Sha256::DigestSize * 2);
    CHECK(hex.starts_with("000f"));
    CHECK(hex.ends_with("ab"));
    // Every hex character must be lowercase (no A-F).
    for (char const c: hex)
        CHECK((c >= '0' && c <= '9' || c >= 'a' && c <= 'f'));
}
