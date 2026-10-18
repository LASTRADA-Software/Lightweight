// SPDX-License-Identifier: Apache-2.0
//
// A ProgressTask that records every call as one line, so a test can assert on the exact sequence a
// library function reports.

#pragma once

#include <Lightweight/ProgressTask.hpp>

#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// Records the calls of a whole task tree into one journal, in call order.
class RecordingProgressTask final: public Lightweight::ProgressTask
{
  public:
    /// The recorded events and the cancellation the tasks report.
    struct Journal
    {
        /// One line per call; see the class comment of each method for the format.
        std::vector<std::string> events;
        /// Asked by `IsCancellationRequested` with the events so far; empty means never cancelled.
        std::function<bool(std::vector<std::string> const&)> cancelWhen;
    };

    /// The root task, path ".", which records no `end`.
    explicit RecordingProgressTask(Journal& journal):
        RecordingProgressTask(journal, ".")
    {
    }

    /// A task recording under @p path.
    RecordingProgressTask(Journal& journal, std::string path):
        _journal { journal },
        _path { std::move(path) }
    {
    }

    RecordingProgressTask(RecordingProgressTask const&) = delete;
    RecordingProgressTask(RecordingProgressTask&&) = delete;
    RecordingProgressTask& operator=(RecordingProgressTask const&) = delete;
    RecordingProgressTask& operator=(RecordingProgressTask&&) = delete;

    /// Records `end <path>` (except for the root).
    ~RecordingProgressTask() override
    {
        if (_path != ".")
            _journal.events.push_back(std::format("end {}", _path));
    }

    /// Records `begin <path> total=<n>`.
    [[nodiscard]] std::unique_ptr<ProgressTask> BeginSubTask(std::string_view title, std::uint64_t total) override
    {
        auto path = _path == "." ? std::string { title } : std::format("{}/{}", _path, title);
        _journal.events.push_back(std::format("begin {} total={}", path, total));
        return std::make_unique<RecordingProgressTask>(_journal, std::move(path));
    }

    /// Records `total <path> <n>`.
    void SetTotal(std::uint64_t total) override
    {
        _journal.events.push_back(std::format("total {} {}", _path, total));
    }

    /// Records `advance <path> +<n>` and, when given, ` <detail>`.
    void Advance(std::uint64_t count, std::string_view detail) override
    {
        _journal.events.push_back(std::format("advance {} +{}{}{}", _path, count, detail.empty() ? "" : " ", detail));
    }

    /// Records `log <path> <level> <message>`.
    void Log(LogLevel level, std::string_view message) override
    {
        auto name = std::string_view { "error" };
        if (level == LogLevel::Info)
            name = "info";
        else if (level == LogLevel::Warning)
            name = "warning";
        _journal.events.push_back(std::format("log {} {} {}", _path, name, message));
    }

    /// @return What `Journal::cancelWhen` says about the events so far.
    [[nodiscard]] bool IsCancellationRequested() const override
    {
        return _journal.cancelWhen && _journal.cancelWhen(_journal.events);
    }

    /// Records `fail <path> <reason>`.
    void Fail(std::string_view reason) override
    {
        _journal.events.push_back(std::format("fail {} {}", _path, reason));
    }

  private:
    Journal& _journal;
    std::string _path;
};
