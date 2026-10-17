// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <utility>

namespace Lightweight::detail
{

/// Constructs a @p T in static storage on first use and never destroys it.
///
/// For state that destructors of other static objects still use during static destruction, whose
/// order relative to this instance no translation unit controls.
///
/// There is one instance per @p T and argument type list, since each is its own specialization, so
/// every call site for the same @p T must pass arguments of the same types.
///
/// @param args The constructor arguments, used by the first call only.
/// @return The one instance.
template <typename T, typename... Args>
T& ImmortalInstance(Args&&... args)
{
    alignas(T) static std::array<std::byte, sizeof(T)> storage {};
    static T& instance = *std::construct_at(reinterpret_cast<T*>(storage.data()), std::forward<Args>(args)...);
    return instance;
}

} // namespace Lightweight::detail
