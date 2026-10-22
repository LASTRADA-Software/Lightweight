// SPDX-License-Identifier: Apache-2.0

#include "ConfigDiscovery.hpp"
#include "ProfileStore.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <system_error>
#include <vector>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif
#if defined(__APPLE__)
    #include <cstdint>

    #include <mach-o/dyld.h>
#endif

namespace Lightweight::Config
{

namespace
{

    /// Collects every `dbtool.yml` at or above `start`, nearest first.
    std::vector<std::filesystem::path> FindAllUpwards(std::filesystem::path const& start)
    {
        auto found = std::vector<std::filesystem::path> {};
        if (start.empty())
            return found;

        std::error_code ec;
        auto dir = std::filesystem::weakly_canonical(std::filesystem::absolute(start, ec), ec);
        if (ec)
            return found;

        while (true)
        {
            auto candidate = dir / ConfigFileName;
            if (std::filesystem::is_regular_file(candidate, ec))
                found.push_back(std::move(candidate));
            if (dir == dir.parent_path())
                return found;
            dir = dir.parent_path();
        }
    }

    /// The directory named by `ConfigSearchDirectoryVariable`, or an empty path when unset or empty.
    std::filesystem::path SearchDirectoryOverride()
    {
        auto const name = std::string { ConfigSearchDirectoryVariable };
        auto const* const value = std::getenv(name.c_str());
        if (value == nullptr || *value == '\0')
            return {};
        std::error_code ec;
        auto absolute = std::filesystem::absolute(std::filesystem::path { value }, ec);
        return ec ? std::filesystem::path { value } : absolute;
    }

} // namespace

std::string_view ToString(ConfigSource source) noexcept
{
    switch (source)
    {
        case ConfigSource::Explicit:
            return "--config";
        case ConfigSource::ExecutableDirectory:
            return "executable directory";
        case ConfigSource::UserDefault:
            return "user default";
        case ConfigSource::None:
            break;
    }
    return "none";
}

std::expected<std::vector<ConfigCandidate>, std::string> FindConfigCandidates(DiscoveryInputs const& inputs)
{
    std::error_code ec;
    if (!inputs.explicitPath.empty())
    {
        if (!std::filesystem::is_regular_file(inputs.explicitPath, ec))
            return std::unexpected(std::format("Config file not found: {}", inputs.explicitPath.string()));
        return std::vector<ConfigCandidate> { { .path = inputs.explicitPath, .source = ConfigSource::Explicit } };
    }

    auto candidates = std::vector<ConfigCandidate> {};
    for (auto& path: FindAllUpwards(inputs.executableDirectory))
        candidates.push_back({ .path = std::move(path), .source = ConfigSource::ExecutableDirectory });

    if (!inputs.userDefault.empty() && std::filesystem::is_regular_file(inputs.userDefault, ec))
    {
        auto const userCanonical = std::filesystem::weakly_canonical(inputs.userDefault, ec);
        auto const alreadyListed = std::ranges::any_of(candidates, [&](ConfigCandidate const& candidate) {
            std::error_code equivalentError;
            return std::filesystem::weakly_canonical(candidate.path, equivalentError) == userCanonical;
        });
        if (!alreadyListed)
            candidates.push_back({ .path = inputs.userDefault, .source = ConfigSource::UserDefault });
    }
    return candidates;
}

DiscoveryInputs DefaultDiscoveryInputs(std::filesystem::path explicitPath)
{
    auto searchDirectory = SearchDirectoryOverride();
    return DiscoveryInputs {
        .explicitPath = std::move(explicitPath),
        .executableDirectory = searchDirectory.empty() ? ExecutableDirectory() : std::move(searchDirectory),
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
