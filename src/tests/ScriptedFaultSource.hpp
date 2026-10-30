// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <Lightweight/SqlError.hpp>
#include <Lightweight/Utils.hpp>

#include <map>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace Lightweight::Tests
{

/// Fails @c failureCount checks with a scripted diagnostic, then lets calls through.
/// Mirrors how a transient outage behaves: a burst of failures followed by recovery.
///
/// Narrows which call site it reacts to via a substring match against the reported
/// @c std::source_location, so an injected fault cannot disturb unrelated library work happening on
/// the same connection. The default, "SqlFaultSeamTests", matches an execution requested from that
/// test file (SqlStatement::ExecuteDirect defaults its `location` parameter to the caller's
/// source_location and forwards it); a call passed explicitly narrows to one specific internal call
/// site instead (e.g. "RetryStalePreparedStatement", "Connect" or "ExecuteBatch"), which is needed for
/// checks the library performs internally and reports its own file/function for.
///
/// This narrowing is a convenience, not a safety requirement: the seam itself skips injection while
/// the handle is still null, so the half-constructed/half-destroyed-object hazard is closed in the
/// library rather than by this filter.
class ScriptedFaultSource: public SqlFaultSource
{
  public:
    /// Scripts @p failureCount failures carrying @p error.
    ///
    /// Deliberately not noexcept: the filter is a std::string built from a string_view, so
    /// construction allocates and can throw. Declaring it noexcept would turn a bad_alloc into a
    /// std::terminate.
    ///
    /// @param error The diagnostic every injected failure carries.
    /// @param failureCount The number of matching checks to fail.
    /// @param locationFilter The substring of the file or function name a check must contain to match.
    /// @param skipCount The number of matching checks let through before the failures start.
    explicit ScriptedFaultSource(SqlErrorInfo error,
                                 int failureCount,
                                 std::string_view locationFilter = "SqlFaultSeamTests",
                                 int skipCount = 0):
        _error { std::move(error) },
        _remaining { failureCount },
        _skip { skipCount },
        _locationFilter { locationFilter }
    {
    }

    /// Scripts failures by position: the matching check with zero-based index `n` among them fails
    /// with the error @p script holds for `n`, if any.
    ///
    /// @param script The error to inject per matching check index.
    /// @param locationFilter The substring of the file or function name a check must contain to match.
    explicit ScriptedFaultSource(std::map<int, SqlErrorInfo> script, std::string_view locationFilter):
        _remaining { 0 },
        _skip { 0 },
        _script { std::move(script) },
        _locationFilter { locationFilter }
    {
    }

    [[nodiscard]] std::optional<SqlErrorInfo> NextFailure(SQLHSTMT /*hStmt*/,
                                                          std::source_location const& sourceLocation) override
    {
        return Consult(sourceLocation);
    }

    [[nodiscard]] std::optional<SqlErrorInfo> NextConnectionFailure(SQLHDBC /*hDbc*/,
                                                                    std::source_location const& sourceLocation) override
    {
        return Consult(sourceLocation);
    }

    /// @return The number of checks that matched the filter so far, skipped and failed ones included.
    [[nodiscard]] int ConsultCount() const noexcept
    {
        return _consultCount;
    }

    /// @return The number of scripted failures not injected yet.
    [[nodiscard]] int RemainingFailures() const noexcept
    {
        return _remaining + static_cast<int>(_script.size());
    }

  private:
    [[nodiscard]] std::optional<SqlErrorInfo> Consult(std::source_location const& sourceLocation)
    {
        auto const file = std::string_view { sourceLocation.file_name() };
        auto const function = std::string_view { sourceLocation.function_name() };
        if (!file.contains(_locationFilter) && !function.contains(_locationFilter))
            return std::nullopt;

        if (auto node = _script.extract(_consultCount++); !node.empty())
            return std::move(node.mapped());
        if (_skip > 0)
        {
            --_skip;
            return std::nullopt;
        }
        if (_remaining <= 0)
            return std::nullopt;

        --_remaining;
        return _error;
    }

    SqlErrorInfo _error {};
    int _remaining;
    int _skip;
    /// Per matching check index: the error injected there.
    std::map<int, SqlErrorInfo> _script;
    int _consultCount = 0;
    std::string _locationFilter;
};

/// Installs a fault source for the duration of a scope and clears it again, so a failing
/// assertion cannot leak the override into later tests.
class ScopedFaultSource
{
  public:
    /// Installs @p source.
    /// @param source The source to install; it must outlive this object.
    explicit ScopedFaultSource(SqlFaultSource* source) noexcept
    {
        SetFaultSource(source);
    }

    ScopedFaultSource(ScopedFaultSource const&) = delete;
    ScopedFaultSource& operator=(ScopedFaultSource const&) = delete;
    ScopedFaultSource(ScopedFaultSource&&) = delete;
    ScopedFaultSource& operator=(ScopedFaultSource&&) = delete;

    ~ScopedFaultSource()
    {
        SetFaultSource(nullptr);
    }
};

} // namespace Lightweight::Tests
