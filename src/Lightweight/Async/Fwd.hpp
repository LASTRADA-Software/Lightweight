// SPDX-License-Identifier: Apache-2.0
#pragma once

// Lightweight forward declarations for the coroutine async layer.
//
// Core headers (SqlConnection, SqlStatement, DataMapper, Pool) include this to declare their
// async member functions without pulling in <coroutine> or the full async runtime. The heavy
// definitions are provided by the corresponding `.cpp` files and by the `Async/*.hpp` template
// bodies (e.g. DataMapperAsync.hpp, Backend.hpp), which include the complete headers.
// <stop_token> is included in full because it is lightweight (no <coroutine>) and the cancellation
// token (a @c std::stop_token) appears as a defaulted argument on async declarations.

#include <stop_token>

namespace Lightweight::Async
{

// Note: the default template argument for Task lives in Task.hpp; it must not be repeated here.
template <typename T>
class Task;

class IExecutor;
class IResumeScheduler;
class IAsyncBackend;
class StrandExecutor;

} // namespace Lightweight::Async
