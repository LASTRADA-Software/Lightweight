// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <format>
#include <stdexcept>
#include <string_view>

namespace Lightweight
{

/// @brief Represents an error when a record is required to be loaded but is not.
///
/// @ingroup DataMapper
class SqlRequireLoadedError: public std::runtime_error
{
  public:
    explicit SqlRequireLoadedError(std::string_view columnType):
        std::runtime_error(std::format("Could not load the data record: {}", columnType))
    {
    }
};

} // namespace Lightweight
