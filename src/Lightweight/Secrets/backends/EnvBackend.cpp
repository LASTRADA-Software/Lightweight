// SPDX-License-Identifier: Apache-2.0

#include "EnvBackend.hpp"

#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace Lightweight::Secrets
{

std::optional<std::string> EnvBackend::Read(std::string_view key)
{
    // `std::getenv` requires a NUL-terminated string. `string_view` does not
    // guarantee that, so we materialise into a `std::string`.
    std::string const keyz { key };
#ifdef _WIN32
    // MSVC flags std::getenv with a deprecation warning; the underlying libc
    // implementation is thread-safe enough for our one-read-at-start-up use.
    #pragma warning(push)
    #pragma warning(disable : 4996)
#endif
    char const* value = std::getenv(keyz.c_str());
#ifdef _WIN32
    #pragma warning(pop)
#endif
    if (value == nullptr)
        return std::nullopt;
    return std::string { value };
}

void EnvBackend::Write(std::string_view /*key*/, std::string_view /*value*/)
{
    throw std::runtime_error("env: secret backend is read-only; use a file or keychain backend instead");
}

void EnvBackend::Erase(std::string_view /*key*/)
{
    throw std::runtime_error("env: secret backend is read-only; use a file or keychain backend instead");
}

std::string EnvBackend::EnvVarForProfile(std::string_view profileName)
{
    std::string result;
    result.reserve(std::string_view("LIGHTWEIGHT__PWD").size() + profileName.size());
    result.append("LIGHTWEIGHT_");
    for (char const c: profileName)
    {
        auto const ch = static_cast<unsigned char>(c);
        if (c == '-' || c == '.' || c == '/' || c == ' ')
            result.push_back('_');
        else
            result.push_back(static_cast<char>(std::toupper(ch)));
    }
    result.append("_PWD");
    return result;
}

} // namespace Lightweight::Secrets
