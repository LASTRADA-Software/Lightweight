// SPDX-License-Identifier: Apache-2.0

#include "Environment.hpp"

#include <cstddef>
#include <cstdlib>

namespace Lightweight::Config
{

std::string EnvironmentValue(char const* name)
{
#ifdef _WIN32
    auto size = std::size_t {};
    if (getenv_s(&size, nullptr, 0, name) != 0 || size == 0)
        return {};
    auto value = std::string(size, '\0');
    if (getenv_s(&size, value.data(), value.size(), name) != 0)
        return {};
    value.resize(size - 1); // size counts the terminating null
    return value;
#else
    auto const* const value = std::getenv(name);
    return value != nullptr ? std::string { value } : std::string {};
#endif
}

} // namespace Lightweight::Config
