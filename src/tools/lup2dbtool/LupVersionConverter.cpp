// SPDX-License-Identifier: Apache-2.0

#include "LupVersionConverter.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <regex>

namespace Lup2DbTool
{

int64_t LupVersion::ToInteger() const noexcept
{
    // Version encoding:
    // For versions < 6.0.0: major * 100 + minor * 10 + patch (e.g., 2.1.6 -> 216)
    // For versions >= 6.0.0: major * 10000 + minor * 100 + patch (e.g., 6.8.8 -> 60808)
    if (major >= 6)
        return (static_cast<int64_t>(major) * 10000) + (minor * 100) + patch;
    return (static_cast<int64_t>(major) * 100) + (minor * 10) + patch;
}

uint64_t LupVersion::ToMigrationTimestamp() const noexcept
{
    // Use fixed prefix 20000000 followed by the version integer
    // This ensures proper ordering and uniqueness
    return 20000000000000ULL + static_cast<uint64_t>(ToInteger());
}

std::string LupVersion::ToString() const
{
    return std::format("{}_{:02}_{:02}", major, minor, patch);
}

std::optional<LupVersion> LupVersion::Parse(std::string_view versionStr)
{
    // Handle formats like "6.8.8" or "6_08_08"
    LupVersion version {};

    // Replace underscores with dots for consistent parsing
    std::string normalized(versionStr);
    std::ranges::replace(normalized, '_', '.');

    // Parse major.minor.patch
    size_t pos1 = normalized.find('.');
    if (pos1 == std::string::npos)
        return std::nullopt;

    size_t pos2 = normalized.find('.', pos1 + 1);
    if (pos2 == std::string::npos)
        return std::nullopt;

    auto parsePart = [](std::string_view str) -> std::optional<int> {
        int value {};
        auto result = std::from_chars(str.data(), str.data() + str.size(), value);
        if (result.ec == std::errc() && result.ptr == str.data() + str.size())
            return value;
        return std::nullopt;
    };

    auto majorOpt = parsePart(std::string_view(normalized).substr(0, pos1));
    auto minorOpt = parsePart(std::string_view(normalized).substr(pos1 + 1, pos2 - pos1 - 1));
    auto patchOpt = parsePart(std::string_view(normalized).substr(pos2 + 1));

    if (!majorOpt || !minorOpt || !patchOpt)
        return std::nullopt;

    version.major = *majorOpt;
    version.minor = *minorOpt;
    version.patch = *patchOpt;

    return version;
}

std::optional<LupVersion> ParseFilename(std::string_view filename)
{
    // Remove .sql extension if present
    if (filename.ends_with(".sql"))
        filename = filename.substr(0, filename.size() - 4);

    std::string filenameStr(filename);

    // Pattern for init_m_MAJOR_MINOR_PATCH
    static std::regex const initPattern(R"(init_m_(\d+)_(\d+)_(\d+))");

    // Pattern for upd_m_MAJOR_MINOR_PATCH
    static std::regex const updPattern(R"(upd_m_(\d+)_(\d+)_(\d+))");

    // Pattern for upd_m_MAJOR_MINOR_PATCH__MAJOR_MINOR_PATCH (range)
    static std::regex const updRangePattern(R"(upd_m_\d+_\d+_\d+__(\d+)_(\d+)_(\d+))");

    std::smatch match;

    // Try range pattern first (takes the second version)
    if (std::regex_match(filenameStr, match, updRangePattern))
    {
        LupVersion version;
        version.major = std::stoi(match[1].str());
        version.minor = std::stoi(match[2].str());
        version.patch = std::stoi(match[3].str());
        return version;
    }

    // Try init pattern
    if (std::regex_match(filenameStr, match, initPattern))
    {
        LupVersion version;
        version.major = std::stoi(match[1].str());
        version.minor = std::stoi(match[2].str());
        version.patch = std::stoi(match[3].str());
        return version;
    }

    // Try update pattern
    if (std::regex_match(filenameStr, match, updPattern))
    {
        LupVersion version;
        version.major = std::stoi(match[1].str());
        version.minor = std::stoi(match[2].str());
        version.patch = std::stoi(match[3].str());
        return version;
    }

    return std::nullopt;
}

bool IsInitMigration(std::string_view filename)
{
    return filename.find("init_m_") != std::string_view::npos;
}

bool IsUpdateMigration(std::string_view filename)
{
    return filename.find("upd_m_") != std::string_view::npos;
}

} // namespace Lup2DbTool
