// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <filesystem>
#include <format>
#include <random>
#include <string_view>

/// Returns a path in the temporary directory named @p stem, a token unique to this test run, and
/// @p extension, so that test runs sharing the temporary directory never touch one another's files.
///
/// @param stem The leading part of the file or directory name; unique within the test run.
/// @param extension Appended after the token, e.g. ".yml"; empty for a directory.
/// @return The path; nothing is created.
inline std::filesystem::path UniqueTempPath(std::string_view stem, std::string_view extension = {})
{
    static auto const runToken =
        std::format("{:x}-{:x}", std::chrono::steady_clock::now().time_since_epoch().count(), std::random_device {}());
    return std::filesystem::temp_directory_path() / std::format("{}-{}{}", stem, runToken, extension);
}
