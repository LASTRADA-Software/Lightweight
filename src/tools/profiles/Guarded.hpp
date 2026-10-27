// SPDX-License-Identifier: Apache-2.0
//
// Private helper: keeps exceptions from crossing the library boundary.

#pragma once

#include <exception>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Lightweight::Profiles::detail
{

/// Runs @p body and converts any exception into `std::unexpected`.
///
/// Callers must never put a secret into an exception message.
///
/// @param context Prefix for the error message, e.g. the name of the operation.
/// @param body Callable returning a `std::expected<T, std::string>`.
/// @return The result of @p body, or `"<context>: <what>"` (`"<context>: unexpected error"` for
///         non-standard exceptions) as an error.
template <typename Body>
[[nodiscard]] auto Guarded(std::string_view context, Body&& body) noexcept -> std::invoke_result_t<Body>
{
    using Result = std::invoke_result_t<Body>;
    try
    {
        try
        {
            return std::forward<Body>(body)();
        }
        catch (std::exception const& exception)
        {
            return Result(std::unexpected(std::format("{}: {}", context, exception.what())));
        }
        catch (...)
        {
            return Result(std::unexpected(std::format("{}: unexpected error", context)));
        }
    }
    catch (...)
    {
        // Building the message itself failed (out of memory): report an empty error, allocation-free.
        return Result(std::unexpect);
    }
}

} // namespace Lightweight::Profiles::detail
