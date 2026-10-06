// SPDX-License-Identifier: Apache-2.0

#include "BackupOutput.hpp"

#include <array>
#include <cctype>
#include <ctime>
#include <format>

namespace Lightweight::Config
{

namespace
{

    /// Local calendar time for `t`, without the platform-specific thread-safety dance at the call site.
    [[nodiscard]] std::tm LocalTime(std::time_t t)
    {
        std::tm local {};
#ifdef _WIN32
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        return local;
    }

    /// Replaces everything that is not safe in a file name on every platform with `_`.
    [[nodiscard]] std::string SafeFileNamePart(std::string_view text)
    {
        auto out = std::string {};
        out.reserve(text.size());
        for (auto const c: text)
        {
            auto const uc = static_cast<unsigned char>(c);
            auto const safe = std::isalnum(uc) != 0 || c == '.' || c == '-' || c == '_' || uc >= 0x80;
            out.push_back(safe ? c : '_');
        }
        return out;
    }

} // namespace

std::string GeneratedBackupFileName(std::string_view profileName, std::chrono::system_clock::time_point now)
{
    auto const local = LocalTime(std::chrono::system_clock::to_time_t(now));
    auto const stem = profileName.empty() ? std::string { "backup" } : SafeFileNamePart(profileName);
    return std::format("{}-{:04}{:02}{:02}-{:02}{:02}{:02}.zip",
                       stem,
                       local.tm_year + 1900,
                       local.tm_mon + 1,
                       local.tm_mday,
                       local.tm_hour,
                       local.tm_min,
                       local.tm_sec);
}

std::optional<BackupOutput> ResolveBackupOutput(BackupOutputRequest const& request)
{
    auto const& requested = request.requested;

    if (requested.empty())
    {
        if (request.folder.empty())
            return std::nullopt;
        return BackupOutput { .path = request.folder / GeneratedBackupFileName(request.profileName, request.now),
                              .inConfiguredFolder = true };
    }

    // "Bare" means no directory part at all: `a.zip`, but not `./a.zip`, `sub/a.zip` or `D:/a.zip`.
    auto const bare = requested.has_filename() && !requested.has_parent_path() && !requested.is_absolute();
    if (bare && !request.folder.empty())
        return BackupOutput { .path = request.folder / requested, .inConfiguredFolder = true };

    return BackupOutput { .path = requested, .inConfiguredFolder = false };
}

} // namespace Lightweight::Config
