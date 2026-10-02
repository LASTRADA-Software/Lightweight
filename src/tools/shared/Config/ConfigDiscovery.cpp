// SPDX-License-Identifier: Apache-2.0

#include "ConfigDiscovery.hpp"
#include "ProfileStore.hpp"

#include <array>
#include <format>
#include <functional>
#include <memory>
#include <optional>
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
    // windows.h must come first
    #include <aclapi.h>
    #include <sddl.h>
#else
    #include <sys/stat.h>

    #include <unistd.h>
#endif
#if defined(__APPLE__)
    #include <cstdint>

    #include <mach-o/dyld.h>
#endif

namespace Lightweight::Config
{

namespace
{

    /// Returns the nearest trusted `dbtool.yml` at or above `start`, if any;
    /// untrusted candidates passed on the way are appended to `skipped`.
    std::optional<std::filesystem::path> FindUpwards(std::filesystem::path const& start,
                                                     std::function<bool(std::filesystem::path const&)> const& isTrusted,
                                                     std::vector<std::filesystem::path>& skipped)
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
            {
                if (isTrusted(candidate))
                    return candidate;
                skipped.push_back(std::move(candidate));
            }
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

    auto const& isTrusted = inputs.isTrusted ? inputs.isTrusted : std::function { &IsTrustedConfigFile };
    auto skipped = std::vector<std::filesystem::path> {};
    auto const result = [&](std::filesystem::path path, ConfigSource source) {
        return DiscoveredConfig { .path = std::move(path), .source = source, .skipped = std::move(skipped) };
    };

    if (auto found = FindUpwards(inputs.workingDirectory, isTrusted, skipped))
        return result(std::move(*found), ConfigSource::WorkingDirectory);

    if (auto found = FindUpwards(inputs.executableDirectory, isTrusted, skipped))
        return result(std::move(*found), ConfigSource::ExecutableDirectory);

    if (!inputs.userDefault.empty() && std::filesystem::is_regular_file(inputs.userDefault, ec))
        return result(inputs.userDefault, ConfigSource::UserDefault);

    return result(inputs.userDefault, ConfigSource::None);
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

#if defined(_WIN32)
namespace
{
    /// String form of the TrustedInstaller service SID (owner of OS-installed files).
    inline constexpr std::wstring_view TrustedInstallerSid =
        L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";

    /// RAII release of memory returned by the security APIs.
    struct LocalFreer
    {
        void operator()(void* memory) const noexcept
        {
            LocalFree(memory);
        }
    };

    /// True when `owner` is the user running this process.
    bool IsCurrentUser(PSID owner)
    {
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
            return false;
        auto const token = std::unique_ptr<void, decltype(&CloseHandle)> { rawToken, &CloseHandle };
        DWORD size = 0;
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
        auto buffer = std::vector<std::byte>(size);
        if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size))
            return false;
        auto const* user = reinterpret_cast<TOKEN_USER const*>(buffer.data());
        return EqualSid(owner, user->User.Sid) != FALSE;
    }

    /// True for the built-in principals that own system- and installer-placed files.
    bool IsSystemPrincipal(PSID owner)
    {
        if (IsWellKnownSid(owner, WinLocalSystemSid) || IsWellKnownSid(owner, WinBuiltinAdministratorsSid))
            return true;
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(owner, &text))
            return false;
        auto const owned = std::unique_ptr<wchar_t, LocalFreer> { text };
        return std::wstring_view { text } == TrustedInstallerSid;
    }
} // namespace

bool IsTrustedConfigFile(std::filesystem::path const& path)
{
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(
            path.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor)
        != ERROR_SUCCESS)
        return false;
    auto const owned = std::unique_ptr<void, LocalFreer> { descriptor };
    return IsCurrentUser(owner) || IsSystemPrincipal(owner);
}
#else
bool IsTrustedConfigFile(std::filesystem::path const& path)
{
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0)
        return false;
    return info.st_uid == ::geteuid() || info.st_uid == 0;
}
#endif

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
