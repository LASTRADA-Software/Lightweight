// SPDX-License-Identifier: Apache-2.0

#include "DataBinder/SqlVariant.hpp"
#include "ImmortalInstance.hpp"
#include "SqlConnectInfo.hpp"
#include "SqlConnection.hpp"
#include "SqlLogger.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <ranges>
#include <utility>
#include <version>

#if __has_include(<stacktrace>)
    #include <stacktrace>
#endif

#if defined(_MSC_VER)
    // Disable warning C4996: This function or variable may be unsafe.
    // It is complaining about getenv, which is fine to use in this case.
    #pragma warning(disable : 4996)
#endif

namespace Lightweight
{

namespace
{

    class SqlStandardLogger: public SqlLogger
    {
      private:
        /// Serializes calls into the message writer, which statements on any thread may reach at once.
        /// Recursive, so a writer that itself logs -- or runs SQL that does -- re-enters instead of
        /// deadlocking.
        std::recursive_mutex _writerMutex;

      protected:
        /// Formats a message and hands it to the message writer, one message at a time.
        template <typename... Args>
        void WriteMessage(std::format_string<Args...> const& fmt, Args&&... args)
        {
            if (!_messageWriter)
                return;
            auto message = std::format(fmt, std::forward<Args>(args)...);
            auto const _ = std::scoped_lock { _writerMutex };
            _messageWriter(std::move(message));
        }

      public:
        SqlStandardLogger(SupportBindLogging supportBindLogging = SupportBindLogging::No):
            SqlLogger { supportBindLogging }
        {
        }

        void OnWarning(std::string_view const& message) override
        {
            WriteMessage("Warning: {}", message);
        }

        void OnError(SqlError error, std::source_location /*sourceLocation*/) override
        {
            WriteMessage("SQL Error: {}", error);
        }

        void OnError(SqlErrorInfo const& errorInfo, std::source_location /*sourceLocation*/) override
        {
            WriteMessage("SQL Error:\n"
                         "  SQLSTATE: {}\n"
                         "  Native error code: {}\n"
                         "  Message: {}",
                         errorInfo.sqlState,
                         errorInfo.nativeErrorCode,
                         errorInfo.message);
        }

        void OnScopedTimerStart(std::string const& /*tag*/) override {}
        void OnScopedTimerStop(std::string const& /*tag*/) override {}
        void OnConnectionOpened(SqlConnection const& /*connection*/) override {}
        void OnConnectionClosed(SqlConnection const& /*connection*/) override {}
        void OnConnectionIdle(SqlConnection const& /*connection*/) override {}
        void OnConnectionReuse(SqlConnection const& /*connection*/) override {}

        void OnExecuteDirect(std::string_view const& /*query*/) override {}
        void OnPrepare(std::string_view const& /*query*/) override {}

        void OnBind(std::string_view const& /*name*/, std::string /*value*/) override {}
        void OnExecute(std::string_view const& /*query*/) override {}
        void OnExecuteBatch() override {}
        void OnFetchRow() override {}
        void OnFetchEnd() override {}
    };

    /// Set once the calling thread's trace state is destroyed, at thread exit -- which for the main thread
    /// comes before static destruction, where statements still report to the logger (#649). Trivially
    /// destructible, so it stays readable for as long as the thread runs any code at all.
    constinit thread_local bool threadTraceRetired = false;

    class SqlTraceLogger: public SqlStandardLogger
    {
        enum class State : uint8_t
        {
            Idle,
            Preparing,
            Executing,
            Fetching,
            Error
        };

        /// What the logger knows of the statement work in progress on one thread.
        ///
        /// Each thread has its own: a statement is driven by one thread at a time -- the async layer
        /// offloads each synchronous call whole -- so statements on different threads neither race on
        /// this state nor lend each other their query or binds.
        struct TraceState
        {
            State state = State::Idle;
            std::string lastPreparedQuery;

            std::chrono::steady_clock::time_point startedAt {};
            std::vector<std::pair<std::string /*name*/, std::string /*value*/>> binds;
            size_t fetchRowCount {};
        };

        /// A thread's TraceState, which marks the thread retired once it is destroyed.
        ///
        /// It writes nothing on the way out: on the main thread that happens after main() returned,
        /// when whatever the message writer refers to may already be gone.
        struct ThreadTrace
        {
            TraceState state;

            ThreadTrace() = default;
            ThreadTrace(ThreadTrace const&) = delete;
            ThreadTrace(ThreadTrace&&) = delete;
            ThreadTrace& operator=(ThreadTrace const&) = delete;
            ThreadTrace& operator=(ThreadTrace&&) = delete;

            ~ThreadTrace()
            {
                threadTraceRetired = true;
            }
        };

        /// Guards the scoped timers, which, unlike statement work, may start and stop on different
        /// threads when a coroutine resumes elsewhere.
        std::mutex _scopedTimersMutex;
        std::unordered_map<std::string /*tag*/, std::chrono::steady_clock::time_point /*startTime*/> _scopedTimers;

        /// @return The calling thread's trace state, or nullptr once the thread has destroyed it.
        static TraceState* Current()
        {
            if (threadTraceRetired)
                return nullptr;
            thread_local auto trace = ThreadTrace {};
            return &trace.state;
        }

      public:
        SqlTraceLogger(SupportBindLogging supportBindLogging = SupportBindLogging::Yes):
            SqlStandardLogger { supportBindLogging }
        {
        }

        void OnError(SqlError error, std::source_location sourceLocation) override
        {
            MarkError();
            SqlStandardLogger::OnError(error, sourceLocation);
            WriteDetails(sourceLocation);
        }

        void OnError(SqlErrorInfo const& errorInfo, std::source_location sourceLocation) override
        {
            MarkError();
            SqlStandardLogger::OnError(errorInfo, sourceLocation);
            WriteDetails(sourceLocation);
        }

        void OnStatementError(SqlErrorInfo const& errorInfo,
                              std::string_view query,
                              std::source_location sourceLocation) override
        {
            MarkError();
            SqlStandardLogger::OnError(errorInfo, sourceLocation);
            WriteDetails(sourceLocation, query);
        }

        void OnScopedTimerStart(std::string const& tag) override
        {
            {
                auto const _ = std::scoped_lock { _scopedTimersMutex };
                _scopedTimers[tag] = std::chrono::steady_clock::now();
            }

            WriteMessage("[{}] Scoped timer started", tag);
        }

        void OnScopedTimerStop(std::string const& tag) override
        {
            auto const end = std::chrono::steady_clock::now();
            auto start = std::optional<std::chrono::steady_clock::time_point> {};
            {
                auto const _ = std::scoped_lock { _scopedTimersMutex };
                if (auto const found = _scopedTimers.find(tag); found != _scopedTimers.end())
                {
                    start = found->second;
                    _scopedTimers.erase(found);
                }
            }

            // Throwing here would terminate the process from ~SqlScopedTimeLogger.
            if (!start)
            {
                WriteMessage("[{}] Scoped timer finished: no matching start", tag);
                return;
            }

            auto const duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - *start);
            WriteMessage("[{}] Scoped timer finished: Took {}ms", tag, duration.count());
        }

        void OnConnectionOpened(SqlConnection const& connection) override
        {
            MarkIdle();
            WriteMessage("Connection {} opened: {}", connection.ConnectionId(), connection.ConnectionString().Sanitized());
        }

        void OnConnectionClosed(SqlConnection const& connection) override
        {
            MarkIdle();
            WriteMessage("Connection {} closed.", connection.ConnectionId());
        }

        void OnConnectionIdle(SqlConnection const& /*connection*/) override
        {
            MarkIdle();
            // nothing to log, for now
        }

        void OnConnectionReuse(SqlConnection const& /*connection*/) override
        {
            // nothing to log, for now
        }

        void OnPrepare(std::string_view const& query) override
        {
            auto* const trace = Current();
            if (!trace)
                return;
            if (trace->state == State::Executing || trace->state == State::Fetching)
                EndFetch(*trace);

            trace->state = State::Preparing;
            trace->lastPreparedQuery = query;
            trace->startedAt = std::chrono::steady_clock::now();
        }

        void OnBind(std::string_view const& name, std::string value) override
        {
            if (auto* const trace = Current())
                trace->binds.emplace_back(name, std::move(value));
        }

        void OnExecuteDirect(std::string_view const& query) override
        {
            auto* const trace = Current();
            if (!trace)
                return;
            if (trace->state == State::Executing || trace->state == State::Fetching)
                EndFetch(*trace);

            trace->state = State::Executing;
            trace->lastPreparedQuery = query;
            trace->startedAt = std::chrono::steady_clock::now();
            trace->fetchRowCount = 0;
        }

        void OnExecute(std::string_view const& query) override
        {
            auto* const trace = Current();
            if (!trace)
                return;
            if (trace->state == State::Executing)
                EndFetch(*trace);

            trace->state = State::Executing;
            trace->lastPreparedQuery = query;
            trace->startedAt = std::chrono::steady_clock::now();
            trace->fetchRowCount = 0;
        }

        void OnExecuteBatch() override
        {
            if (auto* const trace = Current())
                ExecuteBatch(*trace);
        }

        void OnExecuteNativeBatch(std::string_view query) override
        {
            auto* const trace = Current();
            if (!trace)
                return;
            if (trace->state == State::Executing || trace->state == State::Fetching)
                EndFetch(*trace);

            trace->lastPreparedQuery = query;
            ExecuteBatch(*trace);
        }

        void OnFetchRow() override
        {
            if (auto* const trace = Current())
            {
                trace->state = State::Fetching;
                ++trace->fetchRowCount;
            }
        }

        void OnFetchEnd() override
        {
            if (auto* const trace = Current())
                EndFetch(*trace);
        }

      private:
        void MarkError()
        {
            if (auto* const trace = Current())
                trace->state = State::Error;
        }

        void MarkIdle()
        {
            if (auto* const trace = Current())
                trace->state = State::Idle;
        }

        /// Announces a batch execution of the query last prepared on this thread.
        void ExecuteBatch(TraceState& trace)
        {
            WriteMessage("ExecuteBatch: {}", trace.lastPreparedQuery);
            trace.state = State::Executing;
            trace.startedAt = std::chrono::steady_clock::now();
            trace.fetchRowCount = 0;
        }

        /// Writes out the statement work @p trace holds, if any, and resets it.
        void EndFetch(TraceState& trace)
        {
            if (trace.state != State::Executing && trace.state != State::Fetching)
                return;

            // Take the pending work out before writing it, so a writer that runs SQL itself finds this
            // thread idle rather than writing the same statement again.
            auto const pending = std::exchange(trace, TraceState {});

            auto const stoppedAt = std::chrono::steady_clock::now();
            auto const duration = std::chrono::duration_cast<std::chrono::microseconds>(stoppedAt - pending.startedAt);
            auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
            auto const microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration - seconds);
            auto const durationStr = std::format("{}.{:06}", seconds.count(), microseconds.count());

            auto const rowCountStr = [&]() -> std::string {
                if (pending.fetchRowCount == 0)
                    return "";
                if (pending.fetchRowCount == 1)
                    return " [1 row]";
                return std::format(" [{} rows]", pending.fetchRowCount);
            }();

            if (pending.binds.empty())
            {
                WriteMessage("[{}]{} {}", durationStr, rowCountStr, pending.lastPreparedQuery);
                return;
            }

            std::stringstream output;
            size_t count = 0;

            for (auto const& [name, value]: pending.binds)
            {
                if (count)
                    output << ", ";
                ++count;

                if (name.empty())
                    output << std::format("{}", value);
                else
                    output << std::format("{}={}", name, value);
            }

            WriteMessage("[{}]{} {} WITH [{}]", durationStr, rowCountStr, pending.lastPreparedQuery, output.str());
        }

        /// Writes the details of an error.
        ///
        /// This logger is shared by every statement of the calling thread, so what it last heard of is
        /// only a hint, and it is labelled as one: the failed query is named as such only when it is known.
        ///
        /// @param sourceLocation Where the error was reported from.
        /// @param failedQuery The query the error belongs to; empty if that is unknown.
        void WriteDetails(std::source_location sourceLocation, std::string_view failedQuery = {})
        {
            auto stream = std::ostringstream {};

            stream << std::format("  Source: {}:{}", sourceLocation.file_name(), sourceLocation.line());
            if (!failedQuery.empty())
                stream << std::format("  Query: {}", failedQuery);
            else if (auto const* const trace = Current(); trace && !trace->lastPreparedQuery.empty())
                stream << std::format("  Last logged query (not necessarily the failed one): {}", trace->lastPreparedQuery);

#if __has_include(<stacktrace>)
            stream << std::format("  Stack trace:");
            auto stackTrace = std::stacktrace::current(1, 25);
            for (auto const& entry: stackTrace)
                stream << std::format("    {}", entry);
#endif

            WriteMessage("{}", stream.str());
        }
    };

} // namespace

SqlLogger::SqlLogger():
    SqlLogger(SupportBindLogging::No)
{
}

SqlLogger::SqlLogger(SupportBindLogging supportBindLogging, MessageWriter writer):
    _supportsBindLogging { supportBindLogging == SupportBindLogging::Yes }
{
    if (writer)
        _messageWriter = std::move(writer);
    else
        _messageWriter = [](std::string const& message) mutable {
            auto const currentTime = std::chrono::system_clock::now();
            auto const nowMs = time_point_cast<std::chrono::milliseconds>(currentTime);
            auto const currentTimeStr = std::format("{:%F %X}.{:03}", currentTime, nowMs.time_since_epoch().count() % 1'000);
            std::println("[{}] {}", currentTimeStr, message);
        };
}

void SqlLogger::SetLoggingSink(MessageWriter writer)
{
    _messageWriter = std::move(writer);
}

// The built-in loggers are deliberately never destroyed. SQL statements and connections report to the
// current logger from their destructors, and some of them are owned by other statics -- the migration
// manager's DataMapper, for one -- that are torn down during static destruction, in an order relative
// to these loggers that no translation unit controls. A destroyed logger there aborts the process
// with "pure virtual method called" after main() returned (#649).
using detail::ImmortalInstance;

SqlLogger::Null& SqlLogger::NullLogger() noexcept
{
    return ImmortalInstance<SqlLogger::Null>();
}

SqlLogger& SqlLogger::StandardLogger()
{
    return ImmortalInstance<SqlStandardLogger>();
}

SqlLogger& SqlLogger::TraceLogger()
{
    return ImmortalInstance<SqlTraceLogger>(SupportBindLogging::Yes);
}

// Constant-initialized, so it is valid even while other translation units' statics are still being
// initialized; nullptr selects the null logger. Atomic, because every statement on every thread reads
// it while SetLogger() may replace it.
static constinit std::atomic<SqlLogger*> theDefaultLogger = nullptr;

SqlLogger& SqlLogger::GetLogger()
{
    auto* const logger = theDefaultLogger.load(std::memory_order_acquire);
    return logger ? *logger : NullLogger();
}

void SqlLogger::SetLogger(SqlLogger& logger)
{
    theDefaultLogger.store(&logger, std::memory_order_release);
}

} // namespace Lightweight
