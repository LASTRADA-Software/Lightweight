// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the OS-native crypto primitives and `ProfileCipher`, which
// together encrypt the `password` field of dbtool.yml profiles.
//
// The fixed vectors below were computed independently with the OpenSSL CLI, so
// they double as the cross-platform compatibility check: every OS backend (CNG,
// libcrypto, CommonCrypto) must reproduce them bit for bit.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Secrets/Crypto/CryptoPrimitives.hpp>

namespace
{

/// Decodes a lowercase hex string into bytes (test helper; input is trusted).
std::vector<std::byte> FromHex(std::string_view hex)
{
    auto const nibble = [](char c) {
        return static_cast<unsigned>(c >= 'a' ? c - 'a' + 10 : c - '0');
    };
    return hex | std::views::chunk(2) | std::views::transform([&](auto pair) {
               auto const it = pair.begin();
               return static_cast<std::byte>((nibble(*it) << 4U) | nibble(*std::next(it)));
           })
           | std::ranges::to<std::vector>();
}

/// Views a string's characters as bytes.
std::span<std::byte const> AsBytes(std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

/// Converts bytes back into a std::string for comparisons.
std::string ToString(std::span<std::byte const> bytes)
{
    return bytes | std::views::transform([](std::byte b) { return static_cast<char>(b); })
           | std::ranges::to<std::string>();
}

// Dev-key encryption sub-key and IV from the format's reference vector.
constexpr std::string_view VectorEncKeyHex = "020f8e05d0bb8a19855f720b700e723e3d786948c1f08c5f7936bfed5903ef4a";
constexpr std::string_view VectorIvHex = "000102030405060708090a0b0c0d0e0f";

} // namespace

namespace Crypto = Lightweight::Secrets::Crypto;

TEST_CASE("Crypto — HMAC-SHA256 matches RFC 4231 test case 2", "[Crypto]")
{
    auto const mac = Crypto::HmacSha256(AsBytes("Jefe"), AsBytes("what do ya want for nothing?"));
    REQUIRE(mac.has_value());
    auto const expected = FromHex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    CHECK(std::ranges::equal(*mac, expected));
}

TEST_CASE("Crypto — AES-256-CBC matches the OpenSSL reference vector", "[Crypto]")
{
    auto const keyBytes = FromHex(VectorEncKeyHex);
    auto const ivBytes = FromHex(VectorIvHex);
    auto const key = std::span<std::byte const, Crypto::AesKeySize> { keyBytes.data(), Crypto::AesKeySize };
    auto const iv = std::span<std::byte const, Crypto::AesBlockSize> { ivBytes.data(), Crypto::AesBlockSize };

    auto const cipherText = Crypto::AesCbcEncrypt(key, iv, AsBytes("s3cr3t-P@ss"));
    REQUIRE(cipherText.has_value());
    CHECK(std::ranges::equal(*cipherText, FromHex("ab12d6816ed64c081c8e9ad7e118e157")));

    auto const plainText = Crypto::AesCbcDecrypt(key, iv, *cipherText);
    REQUIRE(plainText.has_value());
    CHECK(ToString(*plainText) == "s3cr3t-P@ss");
}

TEST_CASE("Crypto — AES-256-CBC with the wrong key does not recover the plaintext", "[Crypto]")
{
    auto const keyBytes = FromHex(VectorEncKeyHex);
    auto const ivBytes = FromHex(VectorIvHex);
    auto const key = std::span<std::byte const, Crypto::AesKeySize> { keyBytes.data(), Crypto::AesKeySize };
    auto const iv = std::span<std::byte const, Crypto::AesBlockSize> { ivBytes.data(), Crypto::AesBlockSize };
    auto const cipherText = Crypto::AesCbcEncrypt(key, iv, AsBytes("s3cr3t-P@ss"));
    REQUIRE(cipherText.has_value());

    auto wrongKeyBytes = keyBytes;
    wrongKeyBytes[0] ^= std::byte { 0x01 };
    auto const wrongKey = std::span<std::byte const, Crypto::AesKeySize> { wrongKeyBytes.data(), Crypto::AesKeySize };
    auto const decrypted = Crypto::AesCbcDecrypt(wrongKey, iv, *cipherText);
    if (decrypted.has_value())
        CHECK(ToString(*decrypted) != "s3cr3t-P@ss");
}

TEST_CASE("Crypto — RandomBytes produces distinct output on each call", "[Crypto]")
{
    auto first = std::array<std::byte, 32> {};
    auto second = std::array<std::byte, 32> {};
    REQUIRE(Crypto::RandomBytes(first).has_value());
    REQUIRE(Crypto::RandomBytes(second).has_value());
    CHECK(first != second);
}
