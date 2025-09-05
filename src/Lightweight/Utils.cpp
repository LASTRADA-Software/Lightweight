// SPDX-License-Identifier: Apache-2.0

#include "SqlError.hpp"
#include "SqlLogger.hpp"
#include "Utils.hpp"

namespace Lightweight
{

void LogIfFailed(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation)
{
    if (SQL_SUCCEEDED(error))
        return;

    SqlLogger::GetLogger().OnError(SqlErrorInfo::FromStatementHandle(hStmt), sourceLocation);
}

void RequireSuccess(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation)
{
    if (SQL_SUCCEEDED(error))
        return;

    auto const errorInfo = SqlErrorInfo::FromStatementHandle(hStmt);
    if (errorInfo.sqlState == "07009")
    {
        SqlLogger::GetLogger().OnError(errorInfo, sourceLocation);
        throw std::invalid_argument(std::format("SQL error: {}", errorInfo));
    }
    else
        throw SqlException(errorInfo);
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
            result += static_cast<char>(std::toupper(c));
            makeUpper = false;
        }
        else
        {
            result += static_cast<char>(std::tolower(c));
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
