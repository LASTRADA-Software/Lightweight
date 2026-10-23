// SPDX-License-Identifier: Apache-2.0
//
// Portable environment-variable access for the Config helpers (tools-internal, not public API).

#pragma once

#include <string>

namespace Lightweight::Config
{

/// Reads an environment variable; uses `getenv_s` on Windows, where `std::getenv` is deprecated.
/// @param name Name of the variable.
/// @return Its value; empty when it is unset or empty.
[[nodiscard]] std::string EnvironmentValue(char const* name);

} // namespace Lightweight::Config
