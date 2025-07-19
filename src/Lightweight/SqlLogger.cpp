// SPDX-License-Identifier: Apache-2.0

#include "DataBinder/SqlVariant.hpp"
#include "SqlConnectInfo.hpp"
#include "SqlConnection.hpp"
#include "SqlLogger.hpp"

#include <chrono>
#include <cstdlib>
#include <format>
#include <mutex>
#include <print>
#include <ranges>
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
        std::chrono::time_point<std::chrono::system_clock> _currentTime;
        std::string _currentTimeStr;
        std::mutex _mutex;

      protected:
        void Tick()
        {
            auto const _ = std::lock_guard { _mutex };
            _currentTime = std::chrono::system_clock::now();
            auto const nowMs = time_point_cast<std::chrono::milliseconds>(_currentTime);
            _currentTimeStr = std::format("{:%F %X}.{:03}", _currentTime, nowMs.time_since_epoch().count() % 1'000);
        }

        template <typename... Args>
        void WriteMessage(std::format_string<Args...> const& fmt, Args&&... args)
        {
            if (_messageWriter)
                _messageWriter(std::format(fmt, std::forward<Args>(args)...));
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

        State _state = State::Idle;
        std::string _lastPreparedQuery;

        std::chrono::steady_clock::time_point _startedAt {};
        std::vector<std::pair<std::string_view, std::string>> _binds;
        size_t _fetchRowCount {};

        std::unordered_map<std::string /*tag*/, std::chrono::steady_clock::time_point /*startTime*/> _scopedTimers;

      public:
        SqlTraceLogger(SupportBindLogging supportBindLogging = SupportBindLogging::Yes):
            SqlStandardLogger { supportBindLogging }
        {
        }

        void OnError(SqlError error, std::source_location sourceLocation) override
        {
            _state = State::Error;
            SqlStandardLogger::OnError(error, sourceLocation);
            WriteDetails(sourceLocation);
        }

        void OnError(SqlErrorInfo const& errorInfo, std::source_location sourceLocation) override
        {
            _state = State::Error;
            SqlStandardLogger::OnError(errorInfo, sourceLocation);
            WriteDetails(sourceLocation);
        }

        void OnScopedTimerStart(std::string const& tag) override
        {
            _scopedTimers[tag] = std::chrono::steady_clock::now();

            WriteMessage("[{}] Scoped timer started", tag);
        }

        void OnScopedTimerStop(std::string const& tag) override
        {
            auto const start = _scopedTimers.at(tag);
            auto const end = std::chrono::steady_clock::now();
            _scopedTimers.erase(_scopedTimers.find(tag));

            auto const duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            WriteMessage("[{}] Scoped timer finished: Took {}ms", tag, duration.count());
        }

        void OnConnectionOpened(SqlConnection const& connection) override
        {
            _state = State::Idle;
            WriteMessage("Connection {} opened: {}", connection.ConnectionId(), connection.ConnectionString().Sanitized());
        }

        void OnConnectionClosed(SqlConnection const& connection) override
        {
            _state = State::Idle;
            WriteMessage("Connection {} closed.", connection.ConnectionId());
        }

        void OnConnectionIdle(SqlConnection const& /*connection*/) override
        {
            _state = State::Idle;
            // nothing to log, for now
        }

        void OnConnectionReuse(SqlConnection const& /*connection*/) override
        {
            // nothing to log, for now
        }

        void OnPrepare(std::string_view const& query) override
        {
            if (_state == State::Executing || _state == State::Fetching)
                OnFetchEnd();

            _state = State::Preparing;
            _lastPreparedQuery = query;
            _startedAt = std::chrono::steady_clock::now();
        }

        void OnBind(std::string_view const& name, std::string value) override
        {
            _binds.emplace_back(name, std::move(value));
        }

        void OnExecuteDirect(std::string_view const& query) override
        {
            if (_state == State::Executing || _state == State::Fetching)
                OnFetchEnd();

            _state = State::Executing;
            _lastPreparedQuery = query;
            _startedAt = std::chrono::steady_clock::now();
            _fetchRowCount = 0;
        }

        void OnExecute(std::string_view const& query) override
        {
            if (_state == State::Executing)
                OnFetchEnd();

            _state = State::Executing;
            _lastPreparedQuery = query;
            _startedAt = std::chrono::steady_clock::now();
            _fetchRowCount = 0;
        }

        void OnExecuteBatch() override
        {
            WriteMessage("ExecuteBatch: {}", _lastPreparedQuery);
            _state = State::Executing;
            _startedAt = std::chrono::steady_clock::now();
            _fetchRowCount = 0;
        }

        void OnFetchRow() override
        {
            _state = State::Fetching;
            ++_fetchRowCount;
        }

        void OnFetchEnd() override
        {
            if (_state != State::Executing && _state != State::Fetching)
                return;

            auto const stoppedAt = std::chrono::steady_clock::now();
            auto const duration = std::chrono::duration_cast<std::chrono::microseconds>(stoppedAt - _startedAt);
            auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
            auto const microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration - seconds);
            auto const durationStr = std::format("{}.{:06}", seconds.count(), microseconds.count());

            auto const rowCountStr = [&]() -> std::string {
                if (_fetchRowCount == 0)
                    return "";
                if (_fetchRowCount == 1)
                    return " [1 row]";
                return std::format(" [{} rows]", _fetchRowCount);
            }();

            if (_binds.empty())
            {
                WriteMessage("[{}]{} {}", durationStr, rowCountStr, _lastPreparedQuery);
            }
            else
            {
                std::stringstream output;
                size_t count = 0;

                for (auto const& [name, value]: _binds)
                {
                    if (count)
                        output << ", ";
                    ++count;

                    if (name.empty())
                        output << std::format("{}", value);
                    else
                        output << std::format("{}={}", name, value);
                }

                WriteMessage("[{}]{} {} WITH [{}]", durationStr, rowCountStr, _lastPreparedQuery, output.str());
            }

            _lastPreparedQuery.clear();
            _state = State::Idle;
            _fetchRowCount = 0;
            _binds.clear();
        }

      private:
        void WriteDetails(std::source_location sourceLocation)
        {
            auto stream = std::ostringstream {};

            stream << std::format("  Source: {}:{}", sourceLocation.file_name(), sourceLocation.line());
            if (!_lastPreparedQuery.empty())
                stream << std::format("  Query: {}", _lastPreparedQuery);

#if __has_include(<stacktrace>)
            stream << std::format("  Stack trace:");
            auto stackTrace = std::stacktrace::current(1, 25);
            for (std::size_t const i: std::views::iota(std::size_t(0), stackTrace.size()))
                stream << std::format("    [{:>2}] {}", i, stackTrace[i]);
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

SqlLogger::Null& SqlLogger::NullLogger() noexcept
{
    static SqlLogger::Null theNullLogger {};
    return theNullLogger;
}

static std::unique_ptr<SqlStandardLogger> theStdLogger {};

SqlLogger& SqlLogger::StandardLogger()
{
    if (!theStdLogger)
        theStdLogger = std::make_unique<SqlStandardLogger>();

    return *theStdLogger;
}

static std::unique_ptr<SqlTraceLogger> theTraceLogger {};
SqlLogger& SqlLogger::TraceLogger()
{
    if (!theTraceLogger)
        theTraceLogger = std::make_unique<SqlTraceLogger>(SupportBindLogging::Yes);

    return *theTraceLogger;
}

static SqlLogger* theDefaultLogger = &SqlLogger::NullLogger();

SqlLogger& SqlLogger::GetLogger()
{
    return *theDefaultLogger;
}

void SqlLogger::SetLogger(SqlLogger& logger)
{
    theDefaultLogger = &logger;
}

} // namespace Lightweight
