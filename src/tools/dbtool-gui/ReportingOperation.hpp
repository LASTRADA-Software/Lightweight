// SPDX-License-Identifier: Apache-2.0
//
// A restore operation that may hand back the restore's failure report. Test fakes that return
// nothing keep working: a callable returning void stands for "restored, no failures reported".

#pragma once

#include <Lightweight/SqlBackup/SqlBackup.hpp>

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace DbtoolGui
{

/// Type-erased callable returning a `RestoreReport`; built from a callable returning either a
/// `RestoreReport` or void.
/// @tparam Args The operation's parameter types.
template <typename... Args>
class ReportingOperation
{
  public:
    /// Constructs an empty operation, which the owner replaces by its default.
    ReportingOperation() = default;

    /// Wraps a callable (implicitly, so callers pass plain lambdas and function pointers).
    /// @param operation Returns a `RestoreReport` or void (void yields an empty report).
    template <typename Operation>
        requires(!std::same_as<std::remove_cvref_t<Operation>, ReportingOperation>
                 && std::is_invocable_v<Operation&, Args...>)
    ReportingOperation(Operation operation):
        _function([operation = std::move(operation)](Args... args) mutable -> Lightweight::SqlBackup::RestoreReport {
            if constexpr (std::is_void_v<std::invoke_result_t<Operation&, Args...>>)
            {
                operation(std::forward<Args>(args)...);
                return {};
            }
            else
                return operation(std::forward<Args>(args)...);
        })
    {
    }

    /// @return Whether an operation is set.
    explicit operator bool() const noexcept
    {
        return static_cast<bool>(_function);
    }

    /// Runs the operation.
    /// @param args The operation's arguments.
    /// @return What failed to restore; empty for a callable that returns void.
    Lightweight::SqlBackup::RestoreReport operator()(Args... args) const
    {
        return _function(std::forward<Args>(args)...);
    }

  private:
    std::function<Lightweight::SqlBackup::RestoreReport(Args...)> _function;
};

} // namespace DbtoolGui
