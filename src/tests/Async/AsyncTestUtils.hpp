// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Lightweight/Async/ManualExecutor.hpp>
#include <Lightweight/Async/SyncWait.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>

// Helpers for driving a coroutine to completion in tests.
//
// A lambda-coroutine's closure object is NOT copied into the coroutine frame, so a temporary
// `[&]() -> Task<T> { ... }()` dangles the moment that statement ends — yet its body runs later
// (inside SyncWait). Passing the task *factory* by value keeps the closure alive for the whole
// run, which is the correct, footgun-free way to write these tests.

/// Drives the coroutine produced by @p makeTask while pumping @p executor on the calling thread.
template <typename MakeTask>
auto RunPumped(MakeTask makeTask, Lightweight::Async::ManualExecutor& executor)
{
    return Lightweight::Async::SyncWaitPumping(makeTask(), executor);
}

/// Drives the coroutine produced by @p makeTask, blocking the calling thread until it completes.
template <typename MakeTask>
auto RunBlocking(MakeTask makeTask)
{
    return Lightweight::Async::SyncWait(makeTask());
}

/// REQUIREs that @p value is engaged and returns a reference to the contained value.
///
/// Centralizes the optional unwrap so clang-tidy's bugprone-unchecked-optional-access models the
/// REQUIRE on a concrete optional parameter — it does not model REQUIRE on an optional obtained
/// through a function template's deduced return type (such as SyncWaitPumping) at the call site.
template <typename T>
T const& RequireValue(std::optional<T> const& value)
{
    REQUIRE(value.has_value());
    return value.value();
}
