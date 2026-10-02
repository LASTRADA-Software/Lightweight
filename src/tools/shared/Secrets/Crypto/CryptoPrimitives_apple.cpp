// SPDX-License-Identifier: Apache-2.0
//
// macOS implementation of `CryptoPrimitives.hpp` on top of CommonCrypto
// (AES, HMAC) and Security.framework (CSPRNG), both part of the OS.

#include "CryptoPrimitives.hpp"

#include <format>
#include <string_view>

#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonHMAC.h>
#include <Security/SecRandom.h>

namespace Lightweight::Secrets::Crypto
{

namespace
{

    /// Shared body of encrypt/decrypt: `CCCrypt` is a one-shot call for both.
    std::expected<Bytes, std::string> AesCbc(CCOperation operation,
                                             std::span<std::byte const, AesKeySize> key,
                                             std::span<std::byte const, AesBlockSize> iv,
                                             std::span<std::byte const> input)
    {
        auto output = Bytes(input.size() + AesBlockSize);
        std::size_t written = 0;
        auto const status = CCCrypt(operation,
                                    kCCAlgorithmAES,
                                    kCCOptionPKCS7Padding,
                                    key.data(),
                                    key.size(),
                                    iv.data(),
                                    input.data(),
                                    input.size(),
                                    output.data(),
                                    output.size(),
                                    &written);
        if (status != kCCSuccess)
            return std::unexpected(std::format("CCCrypt failed (status {})", static_cast<int>(status)));
        output.resize(written);
        return output;
    }

} // namespace

std::expected<Bytes, std::string> AesCbcEncrypt(std::span<std::byte const, AesKeySize> key,
                                                 std::span<std::byte const, AesBlockSize> iv,
                                                 std::span<std::byte const> plaintext)
{
    return AesCbc(kCCEncrypt, key, iv, plaintext);
}

std::expected<Bytes, std::string> AesCbcDecrypt(std::span<std::byte const, AesKeySize> key,
                                                std::span<std::byte const, AesBlockSize> iv,
                                                std::span<std::byte const> ciphertext)
{
    return AesCbc(kCCDecrypt, key, iv, ciphertext);
}

std::expected<std::array<std::byte, HmacSize>, std::string> HmacSha256(std::span<std::byte const> key,
                                                                       std::span<std::byte const> data)
{
    auto mac = std::array<std::byte, HmacSize> {};
    CCHmac(kCCHmacAlgSHA256, key.data(), key.size(), data.data(), data.size(), mac.data());
    return mac;
}

std::expected<void, std::string> RandomBytes(std::span<std::byte> out)
{
    if (auto const status = SecRandomCopyBytes(kSecRandomDefault, out.size(), out.data()); status != errSecSuccess)
        return std::unexpected(std::format("SecRandomCopyBytes failed (status {})", static_cast<int>(status)));
    return {};
}

} // namespace Lightweight::Secrets::Crypto
