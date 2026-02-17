// SPDX-License-Identifier: Apache-2.0
// Simple SHA-256 implementation for checksum verification
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>

namespace Lightweight::SqlBackup
{

/// Simple SHA-256 implementation for backup integrity verification.
class Sha256
{
  public:
    /// The size of the SHA-256 digest in bytes.
    static constexpr size_t DigestSize = 32;
    /// The block size used by the SHA-256 algorithm in bytes.
    static constexpr size_t BlockSize = 64;

    Sha256()
    {
        Reset();
    }

    /// Resets the hash state to initial values.
    void Reset()
    {
        _state[0] = 0x6a09e667;
        _state[1] = 0xbb67ae85;
        _state[2] = 0x3c6ef372;
        _state[3] = 0xa54ff53a;
        _state[4] = 0x510e527f;
        _state[5] = 0x9b05688c;
        _state[6] = 0x1f83d9ab;
        _state[7] = 0x5be0cd19;
        _count = 0;
        _bufferLen = 0;
    }

    /// Updates the hash with the given data.
    void Update(void const* data, size_t len)
    {
        auto const* bytes = static_cast<uint8_t const*>(data);
        _count += len;

        if (_bufferLen > 0)
        {
            size_t const toCopy = std::min(BlockSize - _bufferLen, len);
            std::memcpy(_buffer.data() + _bufferLen, bytes, toCopy);
            _bufferLen += toCopy;
            bytes += toCopy;
            len -= toCopy;

            if (_bufferLen == BlockSize)
            {
                ProcessBlock(_buffer.data());
                _bufferLen = 0;
            }
        }

        while (len >= BlockSize)
        {
            ProcessBlock(bytes);
            bytes += BlockSize;
            len -= BlockSize;
        }

        if (len > 0)
        {
            std::memcpy(_buffer.data(), bytes, len);
            _bufferLen = len;
        }
    }

    /// Updates the hash with the given data span.
    void Update(std::span<uint8_t const> data)
    {
        Update(data.data(), data.size());
    }

    /// Updates the hash with the given string data.
    void Update(std::string_view data)
    {
        Update(data.data(), data.size());
    }

    /// Finalizes the hash computation and returns the digest.
    std::array<uint8_t, DigestSize> Finalize()
    {
        uint64_t const bitCount = _count * 8;

        // Padding
        uint8_t const pad = 0x80;
        Update(&pad, 1);

        while (_bufferLen != 56)
        {
            uint8_t const zero = 0;
            Update(&zero, 1);
        }

        // Append bit count (big-endian)
        std::array<uint8_t, 8> countBytes {};
        for (size_t i = 0; i < 8; ++i)
            countBytes[i] = static_cast<uint8_t>(bitCount >> (56 - i * 8));
        Update(countBytes.data(), 8);

        // Output hash
        std::array<uint8_t, DigestSize> digest {};
        for (size_t i = 0; i < 8; ++i)
        {
            digest[(i * 4) + 0] = static_cast<uint8_t>(_state[i] >> 24);
            digest[(i * 4) + 1] = static_cast<uint8_t>(_state[i] >> 16);
            digest[(i * 4) + 2] = static_cast<uint8_t>(_state[i] >> 8);
            digest[(i * 4) + 3] = static_cast<uint8_t>(_state[i]);
        }
        return digest;
    }

    /// Converts a digest to its hexadecimal string representation.
    static std::string ToHex(std::array<uint8_t, DigestSize> const& digest)
    {
        std::ostringstream oss;
        for (auto b: digest)
            oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        return oss.str();
    }

    /// Computes the SHA-256 hash of the given data and returns it as a hex string.
    static std::string Hash(void const* data, size_t len)
    {
        Sha256 hasher;
        hasher.Update(data, len);
        return ToHex(hasher.Finalize());
    }

    /// Computes the SHA-256 hash of the given string and returns it as a hex string.
    static std::string Hash(std::string_view data)
    {
        return Hash(data.data(), data.size());
    }

  private:
    static constexpr std::array<uint32_t, 64> K = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };

    static uint32_t RotateRight(uint32_t x, int n)
    {
        return (x >> n) | (x << (32 - n));
    }

    static uint32_t Ch(uint32_t x, uint32_t y, uint32_t z)
    {
        return (x & y) ^ (~x & z);
    }

    static uint32_t Maj(uint32_t x, uint32_t y, uint32_t z)
    {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    static uint32_t Sigma0(uint32_t x)
    {
        return RotateRight(x, 2) ^ RotateRight(x, 13) ^ RotateRight(x, 22);
    }

    static uint32_t Sigma1(uint32_t x)
    {
        return RotateRight(x, 6) ^ RotateRight(x, 11) ^ RotateRight(x, 25);
    }

    static uint32_t LowerSigma0(uint32_t x)
    {
        return RotateRight(x, 7) ^ RotateRight(x, 18) ^ (x >> 3);
    }

    static uint32_t LowerSigma1(uint32_t x)
    {
        return RotateRight(x, 17) ^ RotateRight(x, 19) ^ (x >> 10);
    }

    void ProcessBlock(uint8_t const* block)
    {
        std::array<uint32_t, 64> W {};

        // Prepare message schedule
        for (size_t i = 0; i < 16; ++i)
        {
            W[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[(i * 4) + 1]) << 16)
                   | (static_cast<uint32_t>(block[(i * 4) + 2]) << 8) | static_cast<uint32_t>(block[(i * 4) + 3]);
        }
        for (size_t i = 16; i < 64; ++i)
            W[i] = LowerSigma1(W[i - 2]) + W[i - 7] + LowerSigma0(W[i - 15]) + W[i - 16];

        // Working variables
        uint32_t a = _state[0];
        uint32_t b = _state[1];
        uint32_t c = _state[2];
        uint32_t d = _state[3];
        uint32_t e = _state[4];
        uint32_t f = _state[5];
        uint32_t g = _state[6];
        uint32_t h = _state[7];

        // Compression
        for (size_t i = 0; i < 64; ++i)
        {
            uint32_t const T1 = h + Sigma1(e) + Ch(e, f, g) + K[i] + W[i];
            uint32_t const T2 = Sigma0(a) + Maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + T2;
        }

        _state[0] += a;
        _state[1] += b;
        _state[2] += c;
        _state[3] += d;
        _state[4] += e;
        _state[5] += f;
        _state[6] += g;
        _state[7] += h;
    }

    std::array<uint32_t, 8> _state {};
    std::array<uint8_t, BlockSize> _buffer {};
    size_t _bufferLen = 0;
    uint64_t _count = 0;
};

} // namespace Lightweight::SqlBackup
