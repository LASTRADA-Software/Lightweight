// SPDX-License-Identifier: Apache-2.0

#include "ProfileCipher.hpp"

#include "Crypto/CryptoPrimitives.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace Lightweight::Secrets
{

namespace
{

    inline constexpr std::string_view Base64Alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    inline constexpr std::string_view EncryptionLabel = "dbtool-enc";
    inline constexpr std::string_view MacLabel = "dbtool-mac";

    /// Views a string's characters as bytes.
    std::span<std::byte const> AsBytes(std::string_view text) noexcept
    {
        return std::as_bytes(std::span { text.data(), text.size() });
    }

    /// Standard base64 with `=` padding.
    std::string Base64Encode(std::span<std::byte const> data)
    {
        auto out = std::string {};
        out.reserve(((data.size() + 2) / 3) * 4);
        for (auto const chunk: data | std::views::chunk(3))
        {
            auto const bytes = chunk | std::views::transform([](std::byte b) { return std::to_integer<unsigned>(b); })
                               | std::ranges::to<std::vector>();
            auto const triple = (bytes[0] << 16U) | ((bytes.size() > 1 ? bytes[1] : 0U) << 8U)
                                | (bytes.size() > 2 ? bytes[2] : 0U);
            out.push_back(Base64Alphabet[(triple >> 18U) & 0x3FU]);
            out.push_back(Base64Alphabet[(triple >> 12U) & 0x3FU]);
            out.push_back(bytes.size() > 1 ? Base64Alphabet[(triple >> 6U) & 0x3FU] : '=');
            out.push_back(bytes.size() > 2 ? Base64Alphabet[triple & 0x3FU] : '=');
        }
        return out;
    }

    /// Strict standard base64 decode; any malformed input yields nullopt.
    std::optional<Crypto::Bytes> Base64Decode(std::string_view text)
    {
        if (text.empty() || text.size() % 4 != 0)
            return std::nullopt;
        auto const padding = text.ends_with("==") ? 2U : text.ends_with('=') ? 1U : 0U;
        auto out = Crypto::Bytes {};
        out.reserve((text.size() / 4) * 3);
        for (auto const [quadIndex, quad]: std::views::enumerate(text | std::views::chunk(4)))
        {
            auto const isLast = static_cast<std::size_t>(quadIndex + 1) * 4 == text.size();
            auto value = 0U;
            for (auto const [charIndex, c]: std::views::enumerate(quad))
            {
                auto const paddingSlot = isLast && charIndex >= 4 - static_cast<std::ptrdiff_t>(padding);
                auto const position = Base64Alphabet.find(c);
                if (paddingSlot ? c != '=' : position == std::string_view::npos)
                    return std::nullopt;
                value = (value << 6U) | (paddingSlot ? 0U : static_cast<unsigned>(position));
            }
            auto const produced = isLast ? 3 - padding : 3U;
            for (auto const shift: std::array { 16U, 8U, 0U } | std::views::take(produced))
                out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
        }
        return out;
    }

    /// Constant-time equality of two MACs.
    bool ConstantTimeEqual(std::span<std::byte const> lhs, std::span<std::byte const> rhs) noexcept
    {
        if (lhs.size() != rhs.size())
            return false;
        auto const diff = std::ranges::fold_left(std::views::zip_transform(std::bit_xor<> {}, lhs, rhs),
                                                 std::byte { 0 },
                                                 std::bit_or<> {});
        return diff == std::byte { 0 };
    }

    /// Encryption and MAC sub-keys derived from one master key.
    struct DerivedKeys
    {
        std::array<std::byte, Crypto::HmacSize> encryption {};
        std::array<std::byte, Crypto::HmacSize> mac {};
    };

    std::expected<DerivedKeys, std::string> Derive(KeyEntry const& entry)
    {
        auto encryption = Crypto::HmacSha256(entry.key, AsBytes(EncryptionLabel));
        auto mac = Crypto::HmacSha256(entry.key, AsBytes(MacLabel));
        if (!encryption)
            return std::unexpected(std::move(encryption.error()));
        if (!mac)
            return std::unexpected(std::move(mac.error()));
        return DerivedKeys { .encryption = *encryption, .mac = *mac };
    }

    /// MAC over `enc:<keyId>:` || iv || ciphertext.
    std::expected<std::array<std::byte, Crypto::HmacSize>, std::string> ComputeTag(
        std::span<std::byte const> macKey, std::string_view keyId, std::span<std::byte const> ivAndCipherText)
    {
        auto authenticated = Crypto::Bytes {};
        auto const label = std::format("{}{}:", EncryptedPrefix, keyId);
        authenticated.append_range(AsBytes(label));
        authenticated.append_range(ivAndCipherText);
        return Crypto::HmacSha256(macKey, authenticated);
    }

    /// Default IV source: the OS CSPRNG.
    std::expected<void, std::string> OsRandom(std::span<std::byte> out)
    {
        return Crypto::RandomBytes(out);
    }

} // namespace

ProfileCipher::ProfileCipher(std::vector<KeyEntry> ring, DevKeyPolicy policy, RandomSource random):
    _ring { std::move(ring) },
    _policy { policy },
    _random { random ? std::move(random) : RandomSource { OsRandom } }
{
    if (_ring.empty())
        throw std::invalid_argument("ProfileCipher requires at least one key");
}

ProfileCipher const& ProfileCipher::Builtin()
{
    static auto const instance = ProfileCipher {
        BuiltinKeyRing(),
        BuiltinKeyRingIsRelease() ? DevKeyPolicy::Refuse : DevKeyPolicy::Allow,
    };
    return instance;
}

bool ProfileCipher::IsEncrypted(std::string_view value) noexcept
{
    return value.starts_with(EncryptedPrefix);
}

std::expected<std::string, std::string> ProfileCipher::Encrypt(std::string_view plaintext) const
{
    auto const& entry = _ring.front();
    auto const keys = Derive(entry);
    if (!keys)
        return std::unexpected(keys.error());

    auto iv = std::array<std::byte, Crypto::AesBlockSize> {};
    if (auto const random = _random(iv); !random)
        return std::unexpected(std::format("could not generate an IV: {}", random.error()));

    auto const cipherText = Crypto::AesCbcEncrypt(keys->encryption, iv, AsBytes(plaintext));
    if (!cipherText)
        return std::unexpected(std::format("encryption failed: {}", cipherText.error()));

    auto payload = Crypto::Bytes {};
    payload.append_range(iv);
    payload.append_range(*cipherText);
    auto const tag = ComputeTag(keys->mac, entry.id, payload);
    if (!tag)
        return std::unexpected(tag.error());
    payload.append_range(*tag);

    return std::format("{}{}:{}", EncryptedPrefix, entry.id, Base64Encode(payload));
}

std::expected<std::string, std::string> ProfileCipher::Decrypt(std::string_view value) const
{
    if (!IsEncrypted(value))
        return std::unexpected(std::string { "value is not an encrypted password (expected 'enc:<keyId>:<data>')" });

    auto const body = value.substr(EncryptedPrefix.size());
    auto const separator = body.find(':');
    if (separator == std::string_view::npos || separator == 0)
        return std::unexpected(std::string { "malformed encrypted password (missing key id)" });
    auto const keyId = body.substr(0, separator);

    if (keyId == DevKeyId && _policy == DevKeyPolicy::Refuse)
        return std::unexpected(std::string {
            "this password was encrypted by a development build of dbtool and is not accepted by official builds; "
            "re-run `dbtool add-profile --force` or replace it with the plaintext password to re-encrypt it" });

    auto const entry = std::ranges::find(_ring, keyId, &KeyEntry::id);
    if (entry == _ring.end())
        return std::unexpected(
            std::format("unknown key id '{}' (this dbtool build knows: {}); the value was encrypted by a different build",
                        keyId,
                        _ring | std::views::transform(&KeyEntry::id) | std::views::join_with(std::string_view { ", " })
                            | std::ranges::to<std::string>()));

    auto const payload = Base64Decode(body.substr(separator + 1));
    constexpr auto MinimumSize = Crypto::AesBlockSize * 2 + Crypto::HmacSize;
    if (!payload || payload->size() < MinimumSize || (payload->size() - Crypto::HmacSize) % Crypto::AesBlockSize != 0)
        return std::unexpected(std::string { "malformed encrypted password (bad encoding or length)" });

    auto const keys = Derive(*entry);
    if (!keys)
        return std::unexpected(keys.error());

    auto const bytes = std::span { *payload };
    auto const authenticated = bytes.first(bytes.size() - Crypto::HmacSize);
    auto const storedTag = bytes.last(Crypto::HmacSize);
    auto const expectedTag = ComputeTag(keys->mac, keyId, authenticated);
    if (!expectedTag)
        return std::unexpected(expectedTag.error());
    if (!ConstantTimeEqual(*expectedTag, storedTag))
        return std::unexpected(std::string {
            "encrypted password failed its integrity check (it was modified or encrypted with a different key)" });

    auto const iv = authenticated.first<Crypto::AesBlockSize>();
    auto const plainText = Crypto::AesCbcDecrypt(keys->encryption, iv, authenticated.subspan(Crypto::AesBlockSize));
    if (!plainText)
        return std::unexpected(std::format("decryption failed: {}", plainText.error()));

    return *plainText | std::views::transform([](std::byte b) { return static_cast<char>(b); })
           | std::ranges::to<std::string>();
}

} // namespace Lightweight::Secrets
