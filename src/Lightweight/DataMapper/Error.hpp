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
    /// Constructs the error with the name of the column type that failed to load.
    explicit SqlRequireLoadedError(std::string_view columnType):
        std::runtime_error(std::format("Could not load the data record: {}", columnType))
    {
    }
};

/// @brief Raised when a relation of a record is loaded after the default connection string changed
/// since the record was read.
///
/// Records read through a pool, or through a mapper connected with the default connection string,
/// load their relations from the default database. Once the application has switched it
/// (@ref SqlConnection::SetDefaultConnectionString), loading them there would silently mix two
/// databases, so the load fails instead. Re-read the record, or load its relations before switching.
///
/// @ingroup DataMapper
class SqlDefaultConnectionChangedError: public std::runtime_error
{
  public:
    /// Constructs the error.
    SqlDefaultConnectionChangedError():
        std::runtime_error("Cannot load a relation of a record read before the default connection string changed")
    {
    }
};

} // namespace Lightweight
