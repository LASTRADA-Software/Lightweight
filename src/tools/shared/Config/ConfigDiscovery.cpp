// SPDX-License-Identifier: Apache-2.0

#include "ConfigDiscovery.hpp"

#include "ProfileStore.hpp"

#include <array>
#include <format>
#include <optional>
#include <system_error>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include <cstdint>
    #include <vector>

    #include <mach-o/dyld.h>
#endif

namespace Lightweight::Config
{

namespace
{

    /// Returns the nearest `dbtool.yml` at or above `start`, if any.
    std::optional<std::filesystem::path> FindUpwards(std::filesystem::path const& start)
    {
        if (start.empty())
            return std::nullopt;

        std::error_code ec;
        auto dir = std::filesystem::weakly_canonical(std::filesystem::absolute(start, ec), ec);
        if (ec)
            return std::nullopt;

        while (true)
        {
            auto candidate = dir / ConfigFileName;
            if (std::filesystem::is_regular_file(candidate, ec))
                return candidate;
            if (dir == dir.parent_path())
                return std::nullopt;
            dir = dir.parent_path();
        }
    }

} // namespace

std::string_view ToString(ConfigSource source) noexcept
{
    switch (source)
    {
        case ConfigSource::Explicit:
            return "--config";
        case ConfigSource::WorkingDirectory:
            return "current directory";
        case ConfigSource::ExecutableDirectory:
            return "executable directory";
        case ConfigSource::UserDefault:
            return "user default";
        case ConfigSource::None:
            break;
    }
    return "none";
}

std::expected<DiscoveredConfig, std::string> FindConfigFile(DiscoveryInputs const& inputs)
{
    std::error_code ec;
    if (!inputs.explicitPath.empty())
    {
        if (!std::filesystem::is_regular_file(inputs.explicitPath, ec))
            return std::unexpected(std::format("Config file not found: {}", inputs.explicitPath.string()));
        return DiscoveredConfig { .path = inputs.explicitPath, .source = ConfigSource::Explicit };
    }

    if (auto found = FindUpwards(inputs.workingDirectory))
        return DiscoveredConfig { .path = std::move(*found), .source = ConfigSource::WorkingDirectory };

    if (auto found = FindUpwards(inputs.executableDirectory))
        return DiscoveredConfig { .path = std::move(*found), .source = ConfigSource::ExecutableDirectory };

    if (!inputs.userDefault.empty() && std::filesystem::is_regular_file(inputs.userDefault, ec))
        return DiscoveredConfig { .path = inputs.userDefault, .source = ConfigSource::UserDefault };

    return DiscoveredConfig { .path = inputs.userDefault, .source = ConfigSource::None };
}

DiscoveryInputs DefaultDiscoveryInputs(std::filesystem::path explicitPath)
{
    std::error_code ec;
    return DiscoveryInputs {
        .explicitPath = std::move(explicitPath),
        .workingDirectory = std::filesystem::current_path(ec),
        .executableDirectory = ExecutableDirectory(),
        .userDefault = ProfileStore::DefaultPath(),
    };
}

std::filesystem::path ExecutableDirectory()
{
#if defined(_WIN32)
    auto buffer = std::array<wchar_t, 32768> {};
    auto const length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    return std::filesystem::path { std::wstring_view { buffer.data(), length } }.parent_path();
#elif defined(__APPLE__)
    auto size = std::uint32_t { 0 };
    _NSGetExecutablePath(nullptr, &size);
    auto buffer = std::vector<char>(size + 1);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};
    std::error_code ec;
    return std::filesystem::weakly_canonical(std::filesystem::path { buffer.data() }, ec).parent_path();
#else
    std::error_code ec;
    auto const exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path {} : exe.parent_path();
#endif
}

} // namespace Lightweight::Config
