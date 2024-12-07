// SPDX-License-Identifier: Apache-2.0

#include "SqlGuid.hpp"
#include "StdString.hpp"
#include "BasicStringBinder.hpp"

#if __has_include(<Windows.h>)
    #include <Windows.h>
#elif __has_include(<uuid/uuid.h>)
    #include <cstring>

    #include <uuid/uuid.h>
#endif

#include <memory>
#include <random>
#include <ranges>

SqlGuid SqlGuid::Create() noexcept
{
    SqlGuid guid {};

#if (defined(_WIN32) || defined(_WIN64)) && defined(LIGHTWEIGHT_NATIVE_GUID)

    GUID winGuid;
    CreateGuid(&winGuid);
    guid.data[0] = winGuid.Data1 >> 24;
    guid.data[1] = winGuid.Data1 >> 16;
    guid.data[2] = winGuid.Data1 >> 8;
    guid.data[3] = winGuid.Data1;
    guid.data[4] = winGuid.Data2 >> 8;
    guid.data[5] = winGuid.Data2;
    guid.data[6] = winGuid.Data3 >> 8;
    guid.data[7] = winGuid.Data3;
    guid.data[8] = winGuid.Data4[0];
    guid.data[9] = winGuid.Data4[1];
    guid.data[10] = winGuid.Data4[2];
    guid.data[11] = winGuid.Data4[3];
    guid.data[12] = winGuid.Data4[4];
    guid.data[13] = winGuid.Data4[5];
    guid.data[14] = winGuid.Data4[6];
    guid.data[15] = winGuid.Data4[7];

#elif __has_include(<uuid/uuid.h>)

    uuid_t uuid {};
    uuid_generate_time_safe(uuid);
    std::memcpy(guid.data, uuid, sizeof(uuid));

#else

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned short> dis(0, 255);

    for (auto& byte: guid.data)
        byte = static_cast<uint8_t>(dis(gen));

    // Set the version to 4
    guid.data[6] = (guid.data[6] & 0b0000'1111) | 0b0100'0000;

    // Set the variant to 2
    guid.data[8] = (guid.data[8] & 0b0011'1111) | 0b1000'0000;

#endif
    return guid;
}

std::optional<SqlGuid> SqlGuid::TryParse(std::string_view const& text) noexcept
{
    SqlGuid guid {};

    // UUID format: xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
    // M is the version and N is the variant

    // Check for length
    if (text.size() != 36)
        return std::nullopt;

    // Check for dashes
    if (text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        return std::nullopt;

    // Version must be 1, 2, 3, 4, or 5
    auto const version = text[14];
    if (!('1' <= version && version <= '5'))
        return std::nullopt;

    // Variant must be 8, 9, A, or B
    auto const variant = text[21];
    if (variant != '8' && variant != '9' && variant != 'A' && variant != 'B' && variant != 'a' && variant != 'b')
        return std::nullopt;

    // clang-format off
    size_t i = 0;
    for (auto const index: { 0, 2, 4, 6,
                             9, 11,
                             14, 16,
                             21, 19,
                             24, 26, 28, 30, 32, 34 })
    {
        if (std::from_chars(text.data() + index, text.data() + index + 2, guid.data[i], 16).ec != std::errc())
            return std::nullopt;
        i++;
    }
    // clang-format on

    return guid;
}

SQLRETURN SqlDataBinder<SqlGuid>::InputParameter(SQLHSTMT stmt,
                                                 SQLUSMALLINT column,
                                                 SqlGuid const& value,
                                                 SqlDataBinderCallback& cb) noexcept
{
    switch (cb.ServerType())
    {
        case SqlServerType::SQLITE: {
            // For SQlITE, we'll implement GUIDs as text
            auto text = std::make_shared<std::string>(to_string(value));
            auto rv = SqlDataBinder<std::string>::InputParameter(stmt, column, *text, cb);
            if (SQL_SUCCEEDED(rv))
                cb.PlanPostExecuteCallback([text = std::move(text)] {});
            return rv;
        }
        case SqlServerType::ORACLE:
            return SQLBindParameter(stmt,
                                    column,
                                    SQL_PARAM_INPUT,
                                    SQL_C_BINARY,
                                    SQL_BINARY,
                                    sizeof(value.data),
                                    0,
                                    (SQLPOINTER) &value.data,
                                    sizeof(value),
                                    nullptr);
        case SqlServerType::MYSQL: // TODO
        case SqlServerType::POSTGRESQL:
        case SqlServerType::MICROSOFT_SQL:
        case SqlServerType::UNKNOWN:
            return SQLBindParameter(
                stmt, column, SQL_PARAM_INPUT, SQL_C_GUID, SQL_GUID, sizeof(value), 0, (SQLPOINTER) &value, 0, nullptr);
            break;
    }
    std::unreachable();
}

SQLRETURN SqlDataBinder<SqlGuid>::OutputColumn(
    SQLHSTMT stmt, SQLUSMALLINT column, SqlGuid* result, SQLLEN* indicator, SqlDataBinderCallback& cb) noexcept
{
    switch (cb.ServerType())
    {
        case SqlServerType::SQLITE: {
            // For SQlITE, we'll implement GUIDs as text
            auto text = std::make_shared<std::string>();
            auto rv = SqlDataBinder<std::string>::OutputColumn(stmt, column, text.get(), indicator, cb);
            if (SQL_SUCCEEDED(rv))
                cb.PlanPostProcessOutputColumn(
                    [text = std::move(text), result] { *result = SqlGuid::TryParse(*text).value_or(SqlGuid {}); });
            return rv;
        }
        case SqlServerType::ORACLE:
            return SQLBindCol(stmt, column, SQL_C_BINARY, (SQLPOINTER) result->data, sizeof(result->data), indicator);
        case SqlServerType::MYSQL: // TODO
        case SqlServerType::POSTGRESQL:
        case SqlServerType::MICROSOFT_SQL:
            return SQLBindCol(stmt, column, SQL_C_GUID, (SQLPOINTER) result->data, sizeof(result->data), indicator);
        case SqlServerType::UNKNOWN:
            break;
    }

    std::unreachable();
}

SQLRETURN SqlDataBinder<SqlGuid>::GetColumn(
    SQLHSTMT stmt, SQLUSMALLINT column, SqlGuid* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
{
    switch (cb.ServerType())
    {
        case SqlServerType::SQLITE: {
            // For SQlITE, we'll implement GUIDs as text
            std::string text;
            auto rv = SqlDataBinder<std::string>::GetColumn(stmt, column, &text, indicator, cb);
            if (SQL_SUCCEEDED(rv))
                *result = SqlGuid::TryParse(text).value_or(SqlGuid {});
            return rv;
        }
        case SqlServerType::ORACLE:
            return SQLGetData(stmt, column, SQL_C_BINARY, result->data, sizeof(result->data), indicator);
        case SqlServerType::MYSQL:  // TODO
        case SqlServerType::MICROSOFT_SQL:
        case SqlServerType::POSTGRESQL:
        case SqlServerType::UNKNOWN:
            return SQLGetData(stmt, column, SQL_C_GUID, result->data, sizeof(result->data), indicator);
    }
    std::unreachable();
}
