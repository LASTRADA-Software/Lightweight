// SPDX-License-Identifier: Apache-2.0

#include "SqlError.hpp"
#include "SqlLogger.hpp"
#include "Utils.hpp"

#include <atomic>

namespace Lightweight
{

void LogIfFailed(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation)
{
    if (SQL_SUCCEEDED(error))
        return;

    SqlLogger::GetLogger().OnError(SqlErrorInfo::FromStatementHandle(hStmt), sourceLocation);
}

SqlFailureAction ClassifyOdbcResult(SQLRETURN result, SqlErrorInfo const& errorInfo) noexcept
{
    if (SQL_SUCCEEDED(result))
        return SqlFailureAction::None;

    // Invalid Descriptor Index (07009) is expected when accessing optional ODBC columns
    // that some drivers don't return. Don't report this as an error - the calling code
    // handles it gracefully by catching the exception and using default values.
    if (errorInfo.sqlState == "07009")
        return SqlFailureAction::ThrowInvalidArgument;

    return SqlFailureAction::ThrowSqlException;
}

namespace
{
    // Atomic because the slot is deliberately not thread_local: a test installs a source around a
    // scoped operation and the async layer may run that operation on a pool thread. Install and
    // read therefore happen on different threads, which a plain pointer would make a data race
    // that TSan reports. Relaxed ordering is enough - the pointee's lifetime is owned by the
    // caller, and no data is published through this pointer.
    std::atomic<SqlFaultSource*>& FaultSourceSlot() noexcept
    {
        static std::atomic<SqlFaultSource*> slot { nullptr };
        return slot;
    }
} // namespace

void SetFaultSource(SqlFaultSource* source) noexcept
{
    FaultSourceSlot().store(source, std::memory_order_relaxed);
}

SqlFaultSource* GetFaultSource() noexcept
{
    return FaultSourceSlot().load(std::memory_order_relaxed);
}

void RequireSuccess(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation)
{
    if (SQL_SUCCEEDED(error))
    {
        // Costs one null check on the success path. Only a test ever installs a source; with none
        // configured this is exactly the early return it replaced.
        auto* const faultSource = GetFaultSource();
        if (faultSource == nullptr) [[likely]]
            return;

        // Never inject where the handle is not yet valid. RequireSuccess also guards SQLAllocHandle
        // during SqlStatement construction; throwing there would unwind out of the constructor with
        // the handle already allocated but not yet owned, so the destructor never runs and the
        // handle leaks. Skipping the null handle makes that safe by construction instead of relying
        // on every fault source to filter the call site itself.
        if (hStmt == SQL_NULL_HSTMT)
            return;

        auto injected = faultSource->NextFailure(hStmt, sourceLocation);
        if (!injected)
            return;

        throw SqlException(*std::move(injected));
    }

    auto const errorInfo = SqlErrorInfo::FromStatementHandle(hStmt);
    switch (ClassifyOdbcResult(error, errorInfo))
    {
        case SqlFailureAction::ThrowInvalidArgument:
            throw std::invalid_argument(
                std::format("SQL error: {} in {}:{}", errorInfo, sourceLocation.file_name(), sourceLocation.line()));
        case SqlFailureAction::ThrowSqlException:
            throw SqlException(errorInfo);
        case SqlFailureAction::None:
            break;
    }
}

std::string FormatName(std::string const& name, FormatType formatType)
{
    return FormatName(std::string_view { name }, formatType);
}

std::string FormatName(std::string_view name, FormatType formatType)
{
    if (formatType == FormatType::preserve)
        return std::string { name };

    auto const IsDelimiter = [](char c) {
        return c == '_' || c == '-' || c == ' ';
    };

    std::string result;
    result.reserve(name.size());

    bool makeUpper = false;

    for (auto const c: name)
    {
        if (IsDelimiter(c))
        {
            if (formatType == FormatType::snakeCase)
            {
                result += '_';
            }
            else if (formatType == FormatType::camelCase)
            {
                makeUpper = true;
            }
            continue;
        }
        if (makeUpper)
        {
            // Cast to unsigned char before std::toupper/tolower — passing a signed `char`
            // with the high bit set is undefined behaviour per the standard.
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            makeUpper = false;
        }
        else
        {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    return result;
}

bool UniqueNameBuilder::IsColliding(std::string const& name) const noexcept
{
    return _collisionMap.contains(name);
}

std::optional<std::string> UniqueNameBuilder::TryDeclareName(std::string name)
{
    if (auto const result = _collisionMap.try_emplace(std::move(name), 0); result.second)
        return { result.first->first };
    return std::nullopt;
}

std::string UniqueNameBuilder::DeclareName(std::string name)
{
    auto iter = _collisionMap.find(name);

    if (iter == _collisionMap.end())
    {
        return _collisionMap.insert(std::pair { std::move(name), 1 }).first->first;
    }
    else
    {
        ++iter->second;
        return std::format("{}_{}", iter->first, iter->second);
    }
}

} // namespace Lightweight
