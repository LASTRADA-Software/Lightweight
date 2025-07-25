// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.hpp"
#include "SqlDataBinder.hpp"
#include "SqlError.hpp"

#include <functional>
#include <source_location>
#include <string_view>

namespace Lightweight
{

class SqlConnection;

struct SqlVariant;

/// Represents a logger for SQL operations.
class SqlLogger
{
  public:
    /// Mandates the support for logging bind operations.
    enum class SupportBindLogging : uint8_t
    {
        No,
        Yes
    };

    LIGHTWEIGHT_API SqlLogger();
    LIGHTWEIGHT_API SqlLogger(SqlLogger const& /*other*/) = default;
    LIGHTWEIGHT_API SqlLogger(SqlLogger&& /*other*/) = default;
    LIGHTWEIGHT_API SqlLogger& operator=(SqlLogger const& /*other*/) = default;
    LIGHTWEIGHT_API SqlLogger& operator=(SqlLogger&& /*other*/) = default;
    LIGHTWEIGHT_API virtual ~SqlLogger() = default;

    /// Type definition for a function that writes messages.
    using MessageWriter = std::function<void(std::string /*message*/)>;

    /// Constructs a new logger.
    ///
    /// @param supportBindLogging Indicates if the logger should support bind logging.
    LIGHTWEIGHT_API explicit SqlLogger(SupportBindLogging supportBindLogging, MessageWriter writer = {});

    /// Sets the logging sink for the logger.
    ///
    /// @param writer A function that takes a message string and writes it to the desired output.
    LIGHTWEIGHT_API void SetLoggingSink(MessageWriter writer = {});

    /// Invoked on a warning.
    virtual void OnWarning(std::string_view const& message) = 0;

    /// Invoked on ODBC SQL error occurred.
    virtual void OnError(SqlError errorCode, std::source_location sourceLocation = std::source_location::current()) = 0;

    /// Invoked an ODBC SQL error occurred, with extended error information.
    virtual void OnError(SqlErrorInfo const& errorInfo,
                         std::source_location sourceLocation = std::source_location::current()) = 0;

    /// Invoked when a scoped code region needs to be timed and logged. The region starts with this call.
    virtual void OnScopedTimerStart(std::string const& tag) = 0;

    /// Invoked when a scoped code region needs to be timed and logged. The region ends with this call.
    virtual void OnScopedTimerStop(std::string const& tag) = 0;

    /// Invoked when a connection is opened.
    virtual void OnConnectionOpened(SqlConnection const& connection) = 0;

    /// Invoked when a connection is closed.
    virtual void OnConnectionClosed(SqlConnection const& connection) = 0;

    /// Invoked when a connection is idle.
    virtual void OnConnectionIdle(SqlConnection const& connection) = 0;

    /// Invoked when a connection is reused.
    virtual void OnConnectionReuse(SqlConnection const& connection) = 0;

    /// Invoked when a direct query is executed.
    virtual void OnExecuteDirect(std::string_view const& query) = 0;

    /// Invoked when a query is prepared.
    virtual void OnPrepare(std::string_view const& query) = 0;

    /// Invoked when an input parameter is bound.
    template <typename T>
    void OnBindInputParameter(std::string_view const& name, T&& value)
    {
        if (_supportsBindLogging)
        {
            using value_type = std::remove_cvref_t<T>;
            if constexpr (SqlDataBinderSupportsInspect<value_type>)
            {
                OnBind(name, std::string(SqlDataBinder<value_type>::Inspect(std::forward<T>(value))));
            }
        }
    }

    /// Invoked when an input parameter is bound, by name.
    virtual void OnBind(std::string_view const& name, std::string value) = 0;

    /// Invoked when a prepared query is executed.
    virtual void OnExecute(std::string_view const& query) = 0;

    /// Invoked when a batch of queries is executed
    virtual void OnExecuteBatch() = 0;

    /// Invoked when a row is fetched.
    virtual void OnFetchRow() = 0;

    /// Invoked when fetching is done.
    virtual void OnFetchEnd() = 0;

    class Null;

    /// Retrieves a null logger that does nothing.
    LIGHTWEIGHT_API static Null& NullLogger() noexcept;

    /// Retrieves a logger that logs to standard output.
    LIGHTWEIGHT_API static SqlLogger& StandardLogger();

    /// Retrieves a logger that logs to the trace logger.
    LIGHTWEIGHT_API static SqlLogger& TraceLogger();

    /// Retrieves the currently configured logger.
    LIGHTWEIGHT_API static SqlLogger& GetLogger();

    /// Sets the current logger.
    ///
    /// The ownership of the logger is not transferred and remains with the caller.
    LIGHTWEIGHT_API static void SetLogger(SqlLogger& logger);

  protected:
    MessageWriter _messageWriter; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)

  private:
    bool _supportsBindLogging = false;
};

class SqlLogger::Null: public SqlLogger
{
  public:
    void OnWarning(std::string_view const& /*message*/) override {}
    void OnError(SqlError /*errorCode*/, std::source_location /*sourceLocation*/) override {}
    void OnError(SqlErrorInfo const& /*errorInfo*/, std::source_location /*sourceLocation*/) override {}
    void OnScopedTimerStart(std::string const& /*tag*/) override {}
    void OnScopedTimerStop(std::string const& /*tag*/) override {}
    void OnConnectionOpened(SqlConnection const& /*connection*/) override {}
    void OnConnectionClosed(SqlConnection const& /*connection*/) override {}
    void OnConnectionIdle(SqlConnection const& /*connection*/) override {}
    void OnConnectionReuse(SqlConnection const& /*connection*/) override {}
    void OnExecuteDirect(std::string_view const& /*query*/) override {}
    void OnPrepare(std::string_view const& /*qurey*/) override {}
    void OnBind(std::string_view const& /*name*/, std::string /*value*/) override {}
    void OnExecute(std::string_view const& /*query*/) override {}
    void OnExecuteBatch() override {}
    void OnFetchRow() override {}
    void OnFetchEnd() override {}
};

/// A scoped timer for logging.
///
/// This class is used to measure the time spent in a code region and log it.
/// This is typically useful for performance analysis, to identify bottlenecks.
///
/// @see SqlLogger
class SqlScopedTimeLogger
{
  public:
    SqlScopedTimeLogger(SqlScopedTimeLogger const&) = default;
    SqlScopedTimeLogger(SqlScopedTimeLogger&&) = delete;
    SqlScopedTimeLogger& operator=(SqlScopedTimeLogger const&) = default;
    SqlScopedTimeLogger& operator=(SqlScopedTimeLogger&&) = delete;
    explicit SqlScopedTimeLogger(std::string tag):
        _tag { std::move(tag) }
    {
        SqlLogger::GetLogger().OnScopedTimerStart(_tag);
    }

    ~SqlScopedTimeLogger()
    {
        SqlLogger::GetLogger().OnScopedTimerStop(_tag);
    }

  private:
    std::string _tag;
};

} // namespace Lightweight
