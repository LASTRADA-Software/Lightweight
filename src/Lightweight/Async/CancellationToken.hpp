// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdexcept>

namespace Lightweight::Async
{

/// Thrown when an asynchronous operation is abandoned because cancellation was requested
/// before (or instead of) it producing a result.
class OperationCancelledError: public std::runtime_error
{
  public:
    OperationCancelledError():
        std::runtime_error { "Asynchronous operation was cancelled." }
    {
    }
};

/// @file
/// Cooperative cancellation for the async layer is expressed with the standard @c std::stop_token.
///
/// Async methods accept an optional @c std::stop_token (defaulted to @c {}). A default-constructed
/// token has no associated stop-state — it is @b non-cancellable (@c stop_requested() and
/// @c stop_possible() are always false) and allocates nothing, the cheap default for callers that
/// do not pass one. To cancel, a caller holds a @c std::stop_source, passes @c source.get_token()
/// where supported, and calls @c source.request_stop().
///
/// Cancellation is honored cooperatively and only @e before dispatch: the offload runtime checks
/// @c stop_requested() before posting a step to the worker and, if set, completes it with
/// @ref Lightweight::Async::OperationCancelledError without ever occupying a worker. Once a step has
/// begun running, the in-flight blocking ODBC call is @b not interrupted (there is no @c SQLCancel
/// integration yet), so a request that arrives after dispatch only takes effect on the next
/// not-yet-dispatched step.

} // namespace Lightweight::Async
