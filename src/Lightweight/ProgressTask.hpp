// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace Lightweight
{

/// @brief Progress of one task, which the library reports and a host (a CLI, a GUI) renders.
///
/// Tasks nest: a task opens sub-tasks for its parts, and destroying a task marks it finished. A
/// total of 0 makes a task indeterminate: the host shows that it runs and for how long, not how
/// far it got. The host implements this interface and the library only calls it, possibly from a
/// worker thread, one task tree at a time. New virtual functions are only ever appended at the end.
class ProgressTask
{
  public:
    /// Severity of a message reported against a task.
    enum class LogLevel : std::uint8_t
    {
        Info,
        Warning,
        Error,
    };

    ProgressTask() = default;
    ProgressTask(ProgressTask const&) = delete;
    ProgressTask(ProgressTask&&) = delete;
    ProgressTask& operator=(ProgressTask const&) = delete;
    ProgressTask& operator=(ProgressTask&&) = delete;

    /// Marks the task finished.
    virtual ~ProgressTask() = default;

    /// Opens a child task.
    /// @param title What the child does, e.g. "Unit 2 of 5".
    /// @param total Units of work of the child; 0 = indeterminate.
    /// @return The child; destroying it finishes it.
    [[nodiscard]] virtual std::unique_ptr<ProgressTask> BeginSubTask(std::string_view title, std::uint64_t total) = 0;

    /// Replaces the task's total.
    /// @param total Units of work; 0 = indeterminate.
    virtual void SetTotal(std::uint64_t total) = 0;

    /// Records more work done and names what runs now. `Advance(0, detail)` only changes the detail.
    /// @param count Units of work done since the last call.
    /// @param detail What the task does now; empty keeps the previous detail.
    virtual void Advance(std::uint64_t count = 1, std::string_view detail = {}) = 0;

    /// Reports a message about the task.
    /// @param level The message's severity.
    /// @param message The message, UTF-8.
    virtual void Log(LogLevel level, std::string_view message) = 0;

    /// @return True once the user asked to cancel; callers stop at their next safe point.
    [[nodiscard]] virtual bool IsCancellationRequested() const = 0;

    /// Marks the task failed; destroying it then ends it as failed, not as done.
    /// @param reason What went wrong, UTF-8.
    virtual void Fail(std::string_view reason) = 0;
};

/// @brief A ProgressTask that reports nothing and is never cancelled; used where a host passes none.
class NullProgressTask final: public ProgressTask
{
  public:
    /// @return Another NullProgressTask.
    [[nodiscard]] std::unique_ptr<ProgressTask> BeginSubTask(std::string_view /*title*/, std::uint64_t /*total*/) override
    {
        return std::make_unique<NullProgressTask>();
    }

    /// Does nothing.
    void SetTotal(std::uint64_t /*total*/) override {}

    /// Does nothing.
    void Advance(std::uint64_t /*count*/, std::string_view /*detail*/) override {}

    /// Does nothing.
    void Log(LogLevel /*level*/, std::string_view /*message*/) override {}

    /// @return Always false.
    [[nodiscard]] bool IsCancellationRequested() const override
    {
        return false;
    }

    /// Does nothing.
    void Fail(std::string_view /*reason*/) override {}
};

} // namespace Lightweight
