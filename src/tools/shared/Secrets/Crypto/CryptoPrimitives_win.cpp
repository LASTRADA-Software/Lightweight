// SPDX-License-Identifier: Apache-2.0
//
// Windows implementation of `CryptoPrimitives.hpp` on top of CNG (bcrypt.dll),
// which ships with every supported Windows version.

#include "CryptoPrimitives.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

// clang-format off
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
// clang-format on

namespace Lightweight::Secrets::Crypto
{

namespace
{

    /// RAII owner of a CNG algorithm provider handle.
    struct AlgorithmCloser
    {
        void operator()(void* handle) const noexcept
        {
            BCryptCloseAlgorithmProvider(handle, 0);
        }
    };

    /// RAII owner of a CNG key handle.
    struct KeyDestroyer
    {
        void operator()(void* handle) const noexcept
        {
            BCryptDestroyKey(handle);
        }
    };

    /// RAII owner of a CNG hash handle.
    struct HashDestroyer
    {
        void operator()(void* handle) const noexcept
        {
            BCryptDestroyHash(handle);
        }
    };

    using AlgorithmHandle = std::unique_ptr<std::remove_pointer_t<BCRYPT_ALG_HANDLE>, AlgorithmCloser>;
    using KeyHandle = std::unique_ptr<std::remove_pointer_t<BCRYPT_KEY_HANDLE>, KeyDestroyer>;
    using HashHandle = std::unique_ptr<std::remove_pointer_t<BCRYPT_HASH_HANDLE>, HashDestroyer>;

    /// Formats a failed CNG call for the error channel.
    std::string CngError(std::string_view call, NTSTATUS status)
    {
        return std::format("{} failed (NTSTATUS 0x{:08X})", call, static_cast<unsigned long>(status));
    }

    /// CNG takes non-const `PUCHAR` even for input buffers it does not modify.
    PUCHAR MutableBytes(std::span<std::byte const> bytes) noexcept
    {
        // CNG's API is not const-correct; the buffers are only read.
        return reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data()));
    }

    /// Narrows a buffer size to the `ULONG` CNG expects.
    ULONG Size(std::span<std::byte const> bytes) noexcept
    {
        return static_cast<ULONG>(bytes.size());
    }

    /// Opens an AES provider in CBC mode and imports `key` into it.
    std::expected<std::pair<AlgorithmHandle, KeyHandle>, std::string> MakeAesCbcKey(
        std::span<std::byte const, AesKeySize> key)
    {
        BCRYPT_ALG_HANDLE rawAlgorithm = nullptr;
        if (auto const status = BCryptOpenAlgorithmProvider(&rawAlgorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
            !BCRYPT_SUCCESS(status))
            return std::unexpected(CngError("BCryptOpenAlgorithmProvider(AES)", status));
        auto algorithm = AlgorithmHandle { rawAlgorithm };

        // The property value is a wide string including its terminator; CNG wants the byte count.
        constexpr std::wstring_view ChainMode = BCRYPT_CHAIN_MODE_CBC;
        if (auto const status = BCryptSetProperty(algorithm.get(),
                                                  BCRYPT_CHAINING_MODE,
                                                  reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(ChainMode.data())),
                                                  static_cast<ULONG>((ChainMode.size() + 1) * sizeof(wchar_t)),
                                                  0);
            !BCRYPT_SUCCESS(status))
            return std::unexpected(CngError("BCryptSetProperty(ChainingMode)", status));

        BCRYPT_KEY_HANDLE rawKey = nullptr;
        if (auto const status =
                BCryptGenerateSymmetricKey(algorithm.get(), &rawKey, nullptr, 0, MutableBytes(key), Size(key), 0);
            !BCRYPT_SUCCESS(status))
            return std::unexpected(CngError("BCryptGenerateSymmetricKey", status));

        return std::pair { std::move(algorithm), KeyHandle { rawKey } };
    }

    /// Direction of an AES-CBC operation.
    enum class Direction : std::uint8_t
    {
        Encrypt,
        Decrypt,
    };

    /// Shared body of encrypt/decrypt: CNG's two calls have identical signatures.
    std::expected<Bytes, std::string> AesCbc(Direction direction,
                                             std::span<std::byte const, AesKeySize> key,
                                             std::span<std::byte const, AesBlockSize> iv,
                                             std::span<std::byte const> input)
    {
        auto handles = MakeAesCbcKey(key);
        if (!handles)
            return std::unexpected(std::move(handles.error()));
        auto const& keyHandle = handles->second;

        auto const call = direction == Direction::Encrypt ? &BCryptEncrypt : &BCryptDecrypt;
        auto const callName = direction == Direction::Encrypt ? "BCryptEncrypt" : "BCryptDecrypt";

        // CNG updates the IV buffer in place, so each call gets its own copy.
        auto ivCopy = std::array<std::byte, AesBlockSize> {};
        std::ranges::copy(iv, ivCopy.begin());

        ULONG required = 0;
        if (auto const status = call(keyHandle.get(),
                                     MutableBytes(input),
                                     Size(input),
                                     nullptr,
                                     reinterpret_cast<PUCHAR>(ivCopy.data()),
                                     static_cast<ULONG>(ivCopy.size()),
                                     nullptr,
                                     0,
                                     &required,
                                     BCRYPT_BLOCK_PADDING);
            !BCRYPT_SUCCESS(status))
            return std::unexpected(CngError(callName, status));

        auto output = Bytes(required);
        std::ranges::copy(iv, ivCopy.begin());
        ULONG written = 0;
        if (auto const status = call(keyHandle.get(),
                                     MutableBytes(input),
                                     Size(input),
                                     nullptr,
                                     reinterpret_cast<PUCHAR>(ivCopy.data()),
                                     static_cast<ULONG>(ivCopy.size()),
                                     reinterpret_cast<PUCHAR>(output.data()),
                                     static_cast<ULONG>(output.size()),
                                     &written,
                                     BCRYPT_BLOCK_PADDING);
            !BCRYPT_SUCCESS(status))
            return std::unexpected(CngError(callName, status));

        output.resize(written);
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
    BCRYPT_ALG_HANDLE rawAlgorithm = nullptr;
    if (auto const status =
            BCryptOpenAlgorithmProvider(&rawAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
        !BCRYPT_SUCCESS(status))
        return std::unexpected(CngError("BCryptOpenAlgorithmProvider(HMAC-SHA256)", status));
    auto const algorithm = AlgorithmHandle { rawAlgorithm };

    BCRYPT_HASH_HANDLE rawHash = nullptr;
    if (auto const status = BCryptCreateHash(algorithm.get(), &rawHash, nullptr, 0, MutableBytes(key), Size(key), 0);
        !BCRYPT_SUCCESS(status))
        return std::unexpected(CngError("BCryptCreateHash", status));
    auto const hash = HashHandle { rawHash };

    if (auto const status = BCryptHashData(hash.get(), MutableBytes(data), Size(data), 0); !BCRYPT_SUCCESS(status))
        return std::unexpected(CngError("BCryptHashData", status));

    auto mac = std::array<std::byte, HmacSize> {};
    if (auto const status =
            BCryptFinishHash(hash.get(), reinterpret_cast<PUCHAR>(mac.data()), static_cast<ULONG>(mac.size()), 0);
        !BCRYPT_SUCCESS(status))
        return std::unexpected(CngError("BCryptFinishHash", status));
    return mac;
}

std::expected<void, std::string> RandomBytes(std::span<std::byte> out)
{
    if (auto const status = BCryptGenRandom(nullptr,
                                            reinterpret_cast<PUCHAR>(out.data()),
                                            static_cast<ULONG>(out.size()),
                                            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        !BCRYPT_SUCCESS(status))
        return std::unexpected(CngError("BCryptGenRandom", status));
    return {};
}

} // namespace Lightweight::Secrets::Crypto
