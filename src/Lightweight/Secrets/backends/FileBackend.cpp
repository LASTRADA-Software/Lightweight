// SPDX-License-Identifier: Apache-2.0

#include "FileBackend.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <stdexcept>
#include <vector>

#ifndef _WIN32
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

namespace Lightweight::Secrets
{

namespace
{

    struct Entry
    {
        std::string profile;
        std::string uid;
        std::string password;
    };

    /// Parses a single `profileName:uid:password` line. Empty / comment lines
    /// return `std::nullopt`. Extra `:` characters inside the password are kept
    /// verbatim so passwords containing `:` survive a round-trip.
    std::optional<Entry> ParseLine(std::string_view line)
    {
        // Trim ASCII whitespace from both ends.
        auto const firstNonWs = line.find_first_not_of(" \t\r\n");
        if (firstNonWs == std::string_view::npos)
            return std::nullopt;
        line.remove_prefix(firstNonWs);
        auto const lastNonWs = line.find_last_not_of(" \t\r\n");
        line = line.substr(0, lastNonWs + 1);

        if (line.empty() || line.front() == '#')
            return std::nullopt;

        auto const firstColon = line.find(':');
        if (firstColon == std::string_view::npos)
            return std::nullopt;
        auto const secondColon = line.find(':', firstColon + 1);
        if (secondColon == std::string_view::npos)
            return std::nullopt;

        return Entry {
            .profile = std::string { line.substr(0, firstColon) },
            .uid = std::string { line.substr(firstColon + 1, secondColon - firstColon - 1) },
            .password = std::string { line.substr(secondColon + 1) },
        };
    }

    /// Loads every entry from `path`. Missing file yields an empty vector.
    std::vector<Entry> LoadEntries(std::filesystem::path const& path)
    {
        std::vector<Entry> entries;
        if (!std::filesystem::exists(path))
            return entries;

#ifndef _WIN32
        // Refuse world / group-readable credential files. This mirrors the
        // `.pgpass` convention and catches the classic "forgot to chmod 600"
        // mistake before a secret leaks into CI log output.
        struct stat st {};
        if (stat(path.string().c_str(), &st) == 0)
        {
            auto const badBits = st.st_mode & (S_IRWXG | S_IRWXO);
            if (badBits != 0)
                throw std::runtime_error(
                    std::format("refusing to read {}: mode {:#o} is wider than 0600", path.string(), st.st_mode & 0777));
        }
#endif

        std::ifstream in(path);
        if (!in)
            return entries;

        std::string line;
        while (std::getline(in, line))
            if (auto entry = ParseLine(line))
                entries.push_back(std::move(*entry));
        return entries;
    }

    /// Writes `entries` to `path` atomically: write-then-rename on top of a
    /// `*.tmp` sibling. On POSIX the temp file gets mode 0600 so the rename
    /// preserves it.
    void WriteEntries(std::filesystem::path const& path, std::vector<Entry> const& entries)
    {
        auto const tmp = path.string() + ".tmp";

        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                throw std::runtime_error(std::format("failed to open {} for writing", tmp));
            for (auto const& e: entries)
                out << e.profile << ':' << e.uid << ':' << e.password << '\n';
            if (!out)
                throw std::runtime_error(std::format("failed to write {}", tmp));
        }

#ifndef _WIN32
        if (::chmod(tmp.c_str(), S_IRUSR | S_IWUSR) != 0)
            throw std::runtime_error(std::format("failed to chmod 0600 {}", tmp));
#endif

        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec)
            throw std::runtime_error(std::format("failed to move {} into place: {}", tmp, ec.message()));
    }

} // namespace

FileBackend::FileBackend(std::filesystem::path path):
    _path(std::move(path))
{
}

std::optional<std::string> FileBackend::Read(std::string_view key)
{
    auto const entries = LoadEntries(_path);
    auto const it = std::ranges::find_if(entries, [&](Entry const& e) { return e.profile == key; });
    if (it == entries.end())
        return std::nullopt;
    return it->password;
}

void FileBackend::Write(std::string_view key, std::string_view value)
{
    auto entries = LoadEntries(_path);
    auto it = std::ranges::find_if(entries, [&](Entry const& e) { return e.profile == key; });
    if (it == entries.end())
        entries.push_back(Entry { .profile = std::string { key }, .uid = {}, .password = std::string { value } });
    else
        it->password = std::string { value };

    std::error_code ec;
    std::filesystem::create_directories(_path.parent_path(), ec);
    if (ec)
        throw std::runtime_error(std::format("failed to create {}: {}", _path.parent_path().string(), ec.message()));

    WriteEntries(_path, entries);
}

void FileBackend::Erase(std::string_view key)
{
    auto entries = LoadEntries(_path);
    auto const removed = std::erase_if(entries, [&](Entry const& e) { return e.profile == key; });
    if (removed == 0)
        return;
    WriteEntries(_path, entries);
}

} // namespace Lightweight::Secrets
