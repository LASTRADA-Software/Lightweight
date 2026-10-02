// SPDX-License-Identifier: Apache-2.0

#include "KeyRing.hpp"

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>

namespace Lightweight::Secrets
{

namespace
{

#include "DbtoolKeyRing.inc"

    inline constexpr std::size_t KeySize = 32;
    inline constexpr std::size_t MaxKeyIdLength = 16;

    /// Public development key: SHA-256("lightweight-dbtool-public-development-key").
    inline constexpr std::string_view DevKeyHex = "ba456ca44382fdc8bfd9c84ac989779fa0da3754722e2691f1e0d7a735a3b232";

    /// A key ring entry as stored in the binary: id in clear, key bytes XOR-masked.
    struct MaskedEntry
    {
        std::array<char, MaxKeyIdLength> id {};
        std::size_t idLength = 0;
        std::array<std::uint8_t, KeySize> maskedKey {};
    };

    /// Value of one hex digit; throwing makes a bad spec a compile error.
    consteval std::uint8_t HexDigit(char c)
    {
        if (c >= '0' && c <= '9')
            return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f')
            return static_cast<std::uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F')
            return static_cast<std::uint8_t>(c - 'A' + 10);
        throw std::invalid_argument("dbtool key ring: invalid hex digit");
    }

    /// Parses exactly 64 hex digits into 32 bytes.
    consteval std::array<std::uint8_t, KeySize> ParseKeyHex(std::string_view hex)
    {
        if (hex.size() != KeySize * 2)
            throw std::invalid_argument("dbtool key ring: a key must be 64 hex digits");
        auto bytes = std::array<std::uint8_t, KeySize> {};
        for (auto const index: std::views::iota(std::size_t { 0 }, KeySize))
            bytes[index] = static_cast<std::uint8_t>((HexDigit(hex[index * 2]) << 4U) | HexDigit(hex[(index * 2) + 1]));
        return bytes;
    }

    /// Number of `id=hex` entries in a comma-separated spec.
    consteval std::size_t CountEntries(std::string_view spec)
    {
        return 1 + static_cast<std::size_t>(std::ranges::count(spec, ','));
    }

    /// Parses `id=hex[,id=hex...]` and XOR-masks every key with `mask`.
    template <std::size_t N>
    consteval std::array<MaskedEntry, N> ParseRing(std::string_view spec, std::array<std::uint8_t, KeySize> const& mask)
    {
        auto entries = std::array<MaskedEntry, N> {};
        auto rest = spec;
        for (auto& entry: entries)
        {
            auto const comma = rest.find(',');
            auto const item = rest.substr(0, comma);
            rest = comma == std::string_view::npos ? std::string_view {} : rest.substr(comma + 1);

            auto const equals = item.find('=');
            if (equals == std::string_view::npos || equals == 0 || equals > MaxKeyIdLength)
                throw std::invalid_argument("dbtool key ring: entries must be <id>=<64 hex digits>");
            auto const id = item.substr(0, equals);
            if (!std::ranges::all_of(id, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }))
                throw std::invalid_argument("dbtool key ring: key ids must match [a-z0-9]+");

            std::ranges::copy(id, entry.id.begin());
            entry.idLength = id.size();
            auto const key = ParseKeyHex(item.substr(equals + 1));
            std::ranges::transform(key, mask, entry.maskedKey.begin(), [](std::uint8_t k, std::uint8_t m) {
                return static_cast<std::uint8_t>(k ^ m);
            });
        }
        return entries;
    }

    constinit auto const MaskBytes = ParseKeyHex(DbtoolKeyRingMask);
    constinit auto const MaskedRing =
        ParseRing<CountEntries(DbtoolKeyRingSpec)>(DbtoolKeyRingSpec, ParseKeyHex(DbtoolKeyRingMask));

    /// Recovers a key. The mask is read through a volatile pointer so the
    /// optimiser cannot fold the XOR back into a plain key constant.
    KeyEntry Unmask(MaskedEntry const& masked)
    {
        std::uint8_t const* volatile maskPointer = MaskBytes.data();
        auto const mask = std::span<std::uint8_t const, KeySize> { maskPointer, KeySize };
        auto entry = KeyEntry { .id = std::string(masked.id.data(), masked.idLength), .key = {} };
        std::ranges::transform(masked.maskedKey, mask, entry.key.begin(), [](std::uint8_t k, std::uint8_t m) {
            return static_cast<std::byte>(k ^ m);
        });
        return entry;
    }

} // namespace

KeyEntry DevKey()
{
    auto entry = KeyEntry { .id = std::string { DevKeyId }, .key = {} };
    constexpr auto Key = ParseKeyHex(DevKeyHex);
    std::ranges::transform(Key, entry.key.begin(), [](std::uint8_t b) { return static_cast<std::byte>(b); });
    return entry;
}

std::vector<KeyEntry> BuiltinKeyRing()
{
    auto ring = std::vector<KeyEntry> {};
    ring.reserve(MaskedRing.size());
    for (auto const& masked: MaskedRing)
        ring.push_back(Unmask(masked));
    return ring;
}

bool BuiltinKeyRingIsRelease() noexcept
{
    return DbtoolKeyRingIsRelease;
}

} // namespace Lightweight::Secrets
