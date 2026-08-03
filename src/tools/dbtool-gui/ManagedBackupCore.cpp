// SPDX-License-Identifier: Apache-2.0

#include "ManagedBackupCore.hpp"

#include <Lightweight/SqlConnectInfo.hpp>

#include <format>
#include <fstream>
#include <system_error>
#include <unordered_set>

namespace DbtoolGui::ManagedBackup
{

namespace fs = std::filesystem;

namespace
{

    /// True for the characters a sanitized archive name may contain verbatim.
    [[nodiscard]] constexpr bool IsSafeArchiveNameChar(char c) noexcept
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    }

    /// Number of UTF-8 continuation bytes following a multi-byte lead byte
    /// (0 for ASCII or a stray continuation byte, which cannot start a run).
    [[nodiscard]] constexpr int Utf8ContinuationCount(unsigned char leadByte) noexcept
    {
        if ((leadByte & 0b1110'0000) == 0b1100'0000)
            return 1;
        if ((leadByte & 0b1111'0000) == 0b1110'0000)
            return 2;
        if ((leadByte & 0b1111'1000) == 0b1111'0000)
            return 3;
        return 0;
    }

} // namespace

std::string PathToUtf8(fs::path const& value)
{
    auto const utf8 = value.u8string();
    return std::string { utf8.begin(), utf8.end() };
}

std::string SanitizeArchiveName(std::string_view profileName)
{
    if (profileName.empty())
        return "_";
    auto sanitized = std::string {};
    sanitized.reserve(profileName.size());
    // A multi-byte UTF-8 sequence collapses to a single '_' (one unsafe
    // *character*, not one per byte); continuation bytes of a sequence
    // already mapped to '_' are silently absorbed via this counter.
    auto continuationBytesToSkip = 0;
    for (auto const c: profileName)
    {
        auto const byte = static_cast<unsigned char>(c);
        if (continuationBytesToSkip > 0 && (byte & 0b1100'0000) == 0b1000'0000)
        {
            --continuationBytesToSkip;
            continue;
        }
        if (IsSafeArchiveNameChar(c))
        {
            sanitized.push_back(c);
            continuationBytesToSkip = 0;
            continue;
        }
        sanitized.push_back('_');
        continuationBytesToSkip = Utf8ContinuationCount(byte);
    }
    return sanitized;
}

std::vector<ArchivePlanEntry> PlanArchiveNames(std::vector<std::string> const& profileNames)
{
    auto plan = std::vector<ArchivePlanEntry> {};
    plan.reserve(profileNames.size());
    auto seen = std::unordered_set<std::string> {};
    for (auto const& name: profileNames)
    {
        auto fileName = SanitizeArchiveName(name) + ".zip";
        auto const collision = !seen.insert(fileName).second;
        plan.push_back(ArchivePlanEntry { .profileName = name, .fileName = std::move(fileName), .collision = collision });
    }
    return plan;
}

ArchiveStatus ScanArchive(fs::path const& archivePath)
{
    auto ec = std::error_code {};
    if (!fs::is_regular_file(archivePath, ec) || ec)
        return {};
    // Re-stat under the same window: a file removed between the is_regular_file
    // check and these queries would otherwise leave `sizeBytes` at the
    // sentinel `uintmax_t(-1)` that file_size returns on error. Report it as
    // not-existing so a TOCTOU'd file never surfaces a nonsense size.
    auto const sizeBytes = fs::file_size(archivePath, ec);
    if (ec)
        return {};
    auto const mtime = fs::last_write_time(archivePath, ec);
    if (ec)
        return {};
    return ArchiveStatus {
        .exists = true,
        .sizeBytes = sizeBytes,
        .mtime = mtime,
    };
}

fs::path TempArchivePath(fs::path const& finalPath)
{
    auto tmp = finalPath;
    tmp += ".tmp";
    return tmp;
}

std::expected<void, std::string> CommitArchive(fs::path const& tmpPath, fs::path const& finalPath)
{
    auto ec = std::error_code {};
    fs::rename(tmpPath, finalPath, ec);
    if (ec)
        return std::unexpected(
            std::format("Could not replace {} with {}: {}", PathToUtf8(finalPath), PathToUtf8(tmpPath), ec.message()));
    return {};
}

std::expected<void, std::string> CheckWritableFolder(fs::path const& folder)
{
    auto ec = std::error_code {};
    if (fs::exists(folder, ec) && !fs::is_directory(folder, ec))
        return std::unexpected(std::format("{} exists but is not a directory.", PathToUtf8(folder)));
    fs::create_directories(folder, ec);
    if (ec)
        return std::unexpected(std::format("Could not create {}: {}", PathToUtf8(folder), ec.message()));
    auto const probe = folder / ".dbtool-write-probe";
    if (!std::ofstream(probe).put('x'))
        return std::unexpected(std::format("{} is not writable.", PathToUtf8(folder)));
    // The probe-removal error is intentionally discarded: the write succeeded,
    // so the folder is proven writable regardless of whether the tiny probe
    // file can be cleaned up right now (a lingering probe is harmless and gets
    // overwritten on the next check).
    fs::remove(probe, ec);
    return {};
}

std::string FormatRunSummary(int okCount, int failCount)
{
    if (failCount == 0)
        return std::format("Backed up {} profile(s).", okCount);
    return std::format("Backed up {} profile(s), {} failed.", okCount, failCount);
}

std::expected<std::string, std::string> ResolveConnectionString(Lightweight::Config::Profile const& profile,
                                                                Lightweight::Secrets::SecretResolver const& resolver)
{
    if (!profile.HasConnection())
        return std::unexpected(std::format("Profile '{}' has no connection information.", profile.name));

    auto password = std::string {};
    if (!profile.secretRef.empty())
    {
        auto resolved = resolver.Resolve(profile.secretRef, profile.name);
        if (!resolved)
            return std::unexpected(
                std::format("Could not resolve secret for profile '{}': {}", profile.name, resolved.error().message));
        password = std::move(*resolved);
    }

    // Delegates to the canonical builder for both the raw-connection-string
    // and DSN forms, so a resolved secretRef is honoured either way (a raw
    // connection string with no resolved password is returned unmodified by
    // ToConnectInfo, preserving the "used as-is" contract).
    return std::format("{}", profile.ToConnectInfo(password));
}

} // namespace DbtoolGui::ManagedBackup
