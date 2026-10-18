// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>

namespace Lightweight::Zip
{

/// The bytes libzip expects for a file path: UTF-8, where `path.string()` would give the ANSI code page on Windows.
///
/// @param path The archive file.
/// @return The path as UTF-8 bytes.
[[nodiscard]] inline std::string ZipPathUtf8(std::filesystem::path const& path)
{
    auto const utf8 = path.u8string();
    return std::string { reinterpret_cast<char const*>(utf8.data()), utf8.size() };
}

} // namespace Lightweight::Zip
