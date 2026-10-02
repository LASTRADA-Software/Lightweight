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
#include <expected>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Secrets/Crypto/CryptoPrimitives.hpp>
#include <Secrets/ProfileCipher.hpp>

using namespace std::string_view_literals;

namespace
{

/// The value, or a readable marker for an error. Comparing `std::expected` directly inside
/// `CHECK` trips a recursive-constraint error in trunk libc++.
std::string Unwrapped(std::expected<std::string, std::string> const& result)
{
    return result ? *result : "<error: " + result.error() + ">";
}

/// Decodes a lowercase hex string into bytes (test helper; input is trusted).
std::vector<std::byte> FromHex(std::string_view hex)
{
    auto const nibble = [](char c) {
        return static_cast<unsigned>(c >= 'a' ? c - 'a' + 10 : c - '0');
    };
    auto bytes = std::vector<std::byte> {};
    for (auto const i: std::views::iota(std::size_t { 0 }, hex.size() / 2))
        bytes.push_back(static_cast<std::byte>((nibble(hex[i * 2]) << 4U) | nibble(hex[(i * 2) + 1])));
    return bytes;
}

/// Views a string's characters as bytes.
std::span<std::byte const> AsBytes(std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

/// Converts bytes back into a std::string for comparisons.
std::string ToString(std::span<std::byte const> bytes)
{
    auto text = std::string {};
    for (auto const b: bytes)
        text.push_back(static_cast<char>(b));
    return text;
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

namespace Secrets = Lightweight::Secrets;

namespace
{

/// Reference value: "s3cr3t-P@ss" under the dev key with IV 00..0f.
constexpr std::string_view DevVector =
    "enc:dev:AAECAwQFBgcICQoLDA0OD6sS1oFu1kwIHI6a1+EY4VevOb7jEx+oT8+0ceuOpyZ0qgglTS0t8m638s+GnkICcg==";

/// Reference value: "" under the dev key with IV 00..0f.
constexpr std::string_view DevEmptyVector =
    "enc:dev:AAECAwQFBgcICQoLDA0OD4e/nfkrNXAncseigUrvkuQv1B2I90y+b80luPRR2XRt/W1musybB6QYMi2ImOzsLw==";

/// Deterministic IV source (00 01 .. 0f) so encryption output is reproducible.
std::expected<void, std::string> SequentialIv(std::span<std::byte> out)
{
    for (auto const index: std::views::iota(std::size_t { 0 }, out.size()))
        out[index] = static_cast<std::byte>(index);
    return {};
}

/// Builds a key entry whose 32 key bytes all equal `fill`.
Secrets::KeyEntry FilledKey(std::string id, unsigned char fill)
{
    auto entry = Secrets::KeyEntry { .id = std::move(id), .key = {} };
    std::ranges::fill(entry.key, static_cast<std::byte>(fill));
    return entry;
}

/// Replaces the base64 character at `index` of an `enc:` value's payload with a different one.
std::string TamperPayloadChar(std::string value, std::size_t index)
{
    auto const payloadStart = value.find(':', 4) + 1;
    auto& c = value.at(payloadStart + index);
    c = c == 'A' ? 'B' : 'A';
    return value;
}

} // namespace

TEST_CASE("ProfileCipher — encrypting with a fixed IV reproduces the reference vectors", "[ProfileCipher]")
{
    auto const cipher = Secrets::ProfileCipher { { Secrets::DevKey() }, Secrets::DevKeyPolicy::Allow, SequentialIv };
    CHECK(Unwrapped(cipher.Encrypt("s3cr3t-P@ss")) == DevVector);
    CHECK(Unwrapped(cipher.Encrypt("")) == DevEmptyVector);
}

TEST_CASE("ProfileCipher — reference vectors decrypt on every platform", "[ProfileCipher]")
{
    auto const cipher = Secrets::ProfileCipher { { Secrets::DevKey() }, Secrets::DevKeyPolicy::Allow };
    CHECK(Unwrapped(cipher.Decrypt(DevVector)) == "s3cr3t-P@ss");
    CHECK(Unwrapped(cipher.Decrypt(DevEmptyVector)).empty());
}

TEST_CASE("ProfileCipher — random IVs make each encryption distinct", "[ProfileCipher]")
{
    auto const cipher = Secrets::ProfileCipher { { Secrets::DevKey() }, Secrets::DevKeyPolicy::Allow };
    auto const first = cipher.Encrypt("same");
    auto const second = cipher.Encrypt("same");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first != *second);
    CHECK(Unwrapped(cipher.Decrypt(*first)) == "same");
    CHECK(Unwrapped(cipher.Decrypt(*second)) == "same");
}

TEST_CASE("ProfileCipher — any modified byte fails the integrity check", "[ProfileCipher]")
{
    auto const cipher = Secrets::ProfileCipher { { Secrets::DevKey() }, Secrets::DevKeyPolicy::Allow };
    // Base64 positions inside the IV (bytes 0-15), ciphertext (16-31) and tag (32-63).
    for (auto const index: { std::size_t { 5 }, std::size_t { 30 }, std::size_t { 60 } })
    {
        auto const result = cipher.Decrypt(TamperPayloadChar(std::string { DevVector }, index));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().contains("integrity"));
    }
}

TEST_CASE("ProfileCipher — the key id is authenticated and cannot be relabelled", "[ProfileCipher]")
{
    auto const devAsV1 = Secrets::KeyEntry { .id = "v1", .key = Secrets::DevKey().key };
    auto const cipher = Secrets::ProfileCipher { { Secrets::DevKey(), devAsV1 }, Secrets::DevKeyPolicy::Allow };
    auto relabelled = std::string { DevVector };
    relabelled.replace(4, 3, "v1");
    auto const result = cipher.Decrypt(relabelled);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().contains("integrity"));
}

TEST_CASE("ProfileCipher — unknown key id is reported by name", "[ProfileCipher]")
{
    auto const cipher = Secrets::ProfileCipher { { Secrets::DevKey() }, Secrets::DevKeyPolicy::Allow };
    auto const result = cipher.Decrypt("enc:zz:AAECAwQFBgcICQoLDA0ODw==");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().contains("unknown key id 'zz'"));
}

TEST_CASE("ProfileCipher — release builds refuse values encrypted with the development key", "[ProfileCipher]")
{
    auto const cipher = Secrets::ProfileCipher { { FilledKey("v1", 0x11) }, Secrets::DevKeyPolicy::Refuse };
    auto const result = cipher.Decrypt(DevVector);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().contains("development build"));
}

TEST_CASE("ProfileCipher — key rotation encrypts with the newest key and still decrypts older ones", "[ProfileCipher]")
{
    auto const oldCipher = Secrets::ProfileCipher { { FilledKey("v1", 0x11) }, Secrets::DevKeyPolicy::Refuse };
    auto const oldValue = oldCipher.Encrypt("rotated");
    REQUIRE(oldValue.has_value());

    auto const newCipher =
        Secrets::ProfileCipher { { FilledKey("v2", 0x22), FilledKey("v1", 0x11) }, Secrets::DevKeyPolicy::Refuse };
    auto const newValue = newCipher.Encrypt("rotated");
    REQUIRE(newValue.has_value());
    CHECK(newValue->starts_with("enc:v2:"));
    CHECK(Unwrapped(newCipher.Decrypt(*oldValue)) == "rotated");
}

TEST_CASE("ProfileCipher — malformed values are rejected without crashing", "[ProfileCipher]")
{
    auto const cipher = Secrets::ProfileCipher { { Secrets::DevKey() }, Secrets::DevKeyPolicy::Allow };
    for (auto const value: { "enc:"sv, "enc:dev"sv, "enc:dev:"sv, "enc:dev:!!!!"sv, "enc:dev:AAAA"sv, "enc::AAAA"sv })
        CHECK_FALSE(cipher.Decrypt(value).has_value());
}

TEST_CASE("ProfileCipher — IsEncrypted recognises the enc: prefix only", "[ProfileCipher]")
{
    CHECK(Secrets::ProfileCipher::IsEncrypted("enc:dev:x"));
    CHECK_FALSE(Secrets::ProfileCipher::IsEncrypted("hunter2"));
    CHECK_FALSE(Secrets::ProfileCipher::IsEncrypted(""));
}

TEST_CASE("ProfileCipher — the built-in key ring matches the build flavour", "[ProfileCipher]")
{
    auto const ring = Secrets::BuiltinKeyRing();
    REQUIRE_FALSE(ring.empty());
    CHECK((ring.front().id == Secrets::DevKeyId) == !Secrets::BuiltinKeyRingIsRelease());

    // Whatever ring this build carries, it must round-trip its own output.
    auto const value = Secrets::ProfileCipher::Builtin().Encrypt("builtin");
    REQUIRE(value.has_value());
    CHECK(Unwrapped(Secrets::ProfileCipher::Builtin().Decrypt(*value)) == "builtin");
}
