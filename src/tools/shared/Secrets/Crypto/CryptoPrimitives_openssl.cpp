// SPDX-License-Identifier: Apache-2.0
//
// Linux implementation of `CryptoPrimitives.hpp` on top of the system OpenSSL
// libcrypto (`libssl-dev` at build time, `libssl3` at run time).

#include "CryptoPrimitives.hpp"

#include <array>
#include <climits>
#include <cstdint>
#include <format>
#include <memory>
#include <string_view>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

namespace Lightweight::Secrets::Crypto
{

namespace
{

    /// RAII owner of an OpenSSL cipher context.
    struct CipherContextFree
    {
        void operator()(EVP_CIPHER_CTX* context) const noexcept
        {
            EVP_CIPHER_CTX_free(context);
        }
    };

    using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextFree>;

    /// Formats the most recent OpenSSL error for the error channel.
    std::string OpenSslError(std::string_view call)
    {
        auto buffer = std::array<char, 256> {};
        ERR_error_string_n(ERR_get_error(), buffer.data(), buffer.size());
        return std::format("{} failed: {}", call, buffer.data());
    }

    /// OpenSSL takes `unsigned char const*`; spans of std::byte are reinterpreted.
    unsigned char const* Ptr(std::span<std::byte const> bytes) noexcept
    {
        return reinterpret_cast<unsigned char const*>(bytes.data());
    }

    /// Direction of an AES-CBC operation (OpenSSL's `enc` flag).
    enum class Direction : std::uint8_t
    {
        Decrypt = 0,
        Encrypt = 1,
    };

    /// Shared body of encrypt/decrypt via the EVP cipher-update API.
    std::expected<Bytes, std::string> AesCbc(Direction direction,
                                             std::span<std::byte const, AesKeySize> key,
                                             std::span<std::byte const, AesBlockSize> iv,
                                             std::span<std::byte const> input)
    {
        if (input.size() > static_cast<std::size_t>(INT_MAX - static_cast<int>(AesBlockSize)))
            return std::unexpected(std::string { "AES-CBC input too large" });

        auto const context = CipherContext { EVP_CIPHER_CTX_new() };
        if (!context)
            return std::unexpected(OpenSslError("EVP_CIPHER_CTX_new"));

        if (EVP_CipherInit_ex(context.get(), EVP_aes_256_cbc(), nullptr, Ptr(key), Ptr(iv), static_cast<int>(direction))
            != 1)
            return std::unexpected(OpenSslError("EVP_CipherInit_ex"));

        auto output = Bytes(input.size() + AesBlockSize);
        auto* const out = reinterpret_cast<unsigned char*>(output.data());
        int updateLength = 0;
        if (EVP_CipherUpdate(context.get(), out, &updateLength, Ptr(input), static_cast<int>(input.size())) != 1)
            return std::unexpected(OpenSslError("EVP_CipherUpdate"));

        int finalLength = 0;
        if (EVP_CipherFinal_ex(context.get(), out + updateLength, &finalLength) != 1)
            return std::unexpected(OpenSslError("EVP_CipherFinal_ex"));

        output.resize(static_cast<std::size_t>(updateLength) + static_cast<std::size_t>(finalLength));
        return output;
    }

} // namespace

std::expected<Bytes, std::string> AesCbcEncrypt(std::span<std::byte const, AesKeySize> key,
                                                std::span<std::byte const, AesBlockSize> iv,
                                                std::span<std::byte const> plaintext)
{
    return AesCbc(Direction::Encrypt, key, iv, plaintext);
}

std::expected<Bytes, std::string> AesCbcDecrypt(std::span<std::byte const, AesKeySize> key,
                                                std::span<std::byte const, AesBlockSize> iv,
                                                std::span<std::byte const> ciphertext)
{
    return AesCbc(Direction::Decrypt, key, iv, ciphertext);
}

std::expected<std::array<std::byte, HmacSize>, std::string> HmacSha256(std::span<std::byte const> key,
                                                                       std::span<std::byte const> data)
{
    if (key.size() > static_cast<std::size_t>(INT_MAX))
        return std::unexpected(std::string { "HMAC key too large" });

    auto mac = std::array<std::byte, HmacSize> {};
    unsigned int macLength = 0;
    if (HMAC(EVP_sha256(),
             key.data(),
             static_cast<int>(key.size()),
             Ptr(data),
             data.size(),
             reinterpret_cast<unsigned char*>(mac.data()),
             &macLength)
            == nullptr
        || macLength != HmacSize)
        return std::unexpected(OpenSslError("HMAC"));
    return mac;
}

std::expected<void, std::string> RandomBytes(std::span<std::byte> out)
{
    if (out.size() > static_cast<std::size_t>(INT_MAX))
        return std::unexpected(std::string { "random request too large" });
    if (RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), static_cast<int>(out.size())) != 1)
        return std::unexpected(OpenSslError("RAND_bytes"));
    return {};
}

} // namespace Lightweight::Secrets::Crypto
