// SPDX-License-Identifier: Apache-2.0

#include "SqlError.hpp"
#include "SqlLogger.hpp"
#include "Utils.hpp"

void LogIfFailed(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation)
{
    if (SQL_SUCCEEDED(error))
        return;

    SqlLogger::GetLogger().OnError(SqlErrorInfo::fromStatementHandle(hStmt), sourceLocation);
}

void RequireSuccess(SQLHSTMT hStmt, SQLRETURN error, std::source_location sourceLocation)
{
    if (SQL_SUCCEEDED(error))
        return;

    auto errorInfo = SqlErrorInfo::fromStatementHandle(hStmt);
    if (errorInfo.sqlState == "07009")
    {
        SqlLogger::GetLogger().OnError(errorInfo, sourceLocation);
        throw std::invalid_argument(std::format("SQL error: {}", errorInfo));
    }
    else
        throw SqlException(std::move(errorInfo));
}

std::string formatName(std::string_view name, FormatType formatType)
{
    if (formatType == FormatType::existing)
        return std::string { name };

    const auto IsDelimiter = [](char c) {
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
