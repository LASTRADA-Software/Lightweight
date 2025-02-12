// SPDX-License-Identifier: Apache-2.0

#include "BasicStringBinder.hpp"
#include "SqlVariant.hpp"

SQLRETURN SqlDataBinder<SqlVariant>::InputParameter(SQLHSTMT stmt,
                                                    SQLUSMALLINT column,
                                                    SqlVariant const& variantValue,
                                                    SqlDataBinderCallback& cb) noexcept
{
    return std::visit(detail::overloaded { [&]<typename T>(T const& value) {
                          return SqlDataBinder<T>::InputParameter(stmt, column, value, cb);
                      } },
                      variantValue.value);
}

SQLRETURN SqlDataBinder<SqlVariant>::GetColumn(
    SQLHSTMT stmt, SQLUSMALLINT column, SqlVariant* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
{
    SQLLEN columnType {};
    SQLRETURN returnCode =
        SQLColAttributeA(stmt, static_cast<SQLSMALLINT>(column), SQL_DESC_TYPE, nullptr, 0, nullptr, &columnType);
    if (!SQL_SUCCEEDED(returnCode))
        return returnCode;

    auto& variant = result->value;

    switch (columnType)
    {
        case SQL_BIT:
            returnCode = SqlDataBinder<bool>::GetColumn(stmt, column, &variant.emplace<bool>(), indicator, cb);
            break;
        case SQL_TINYINT:
            returnCode = SqlDataBinder<int8_t>::GetColumn(stmt, column, &variant.emplace<int8_t>(), indicator, cb);
            break;
        case SQL_SMALLINT:
            returnCode = SqlDataBinder<unsigned short>::GetColumn(
                stmt, column, &variant.emplace<unsigned short>(), indicator, cb);
            break;
        case SQL_INTEGER:
            returnCode = SqlDataBinder<int>::GetColumn(stmt, column, &variant.emplace<int>(), indicator, cb);
            break;
        case SQL_BIGINT:
            returnCode =
                SqlDataBinder<long long>::GetColumn(stmt, column, &variant.emplace<long long>(), indicator, cb);
            break;
        case SQL_REAL:
            returnCode = SqlDataBinder<float>::GetColumn(stmt, column, &variant.emplace<float>(), indicator, cb);
            break;
        case SQL_FLOAT:
        case SQL_DOUBLE:
            returnCode = SqlDataBinder<double>::GetColumn(stmt, column, &variant.emplace<double>(), indicator, cb);
            break;
        case SQL_CHAR:          // fixed-length string
        case SQL_VARCHAR:       // variable-length string
        case SQL_LONGVARCHAR:   // long string
            returnCode =
                SqlDataBinder<std::string>::GetColumn(stmt, column, &variant.emplace<std::string>(), indicator, cb);
            break;
        case SQL_WCHAR:         // fixed-length Unicode (UTF-16) string
        case SQL_WVARCHAR:      // variable-length Unicode (UTF-16) string
        case SQL_WLONGVARCHAR:  // long Unicode (UTF-16) string
            returnCode =
                SqlDataBinder<std::u16string>::GetColumn(stmt, column, &variant.emplace<std::u16string>(), indicator, cb);
            break;
        case SQL_BINARY:        // fixed-length binary
        case SQL_VARBINARY:     // variable-length binary
        case SQL_LONGVARBINARY: // long binary
            returnCode =
                SqlDataBinder<std::string>::GetColumn(stmt, column, &variant.emplace<std::string>(), indicator, cb);
            break;
        case SQL_DATE:
            // Oracle ODBC driver returns SQL_DATE for DATE columns
            returnCode =
                SqlDataBinder<SqlDateTime>::GetColumn(stmt, column, &variant.emplace<SqlDateTime>(), indicator, cb);
            break;
        case SQL_TYPE_DATE:
            returnCode = SqlDataBinder<SqlDate>::GetColumn(stmt, column, &variant.emplace<SqlDate>(), indicator, cb);
            break;
        case SQL_TIME:
            SqlLogger::GetLogger().OnWarning(
                std::format("SQL_TIME is from ODBC 2. SQL_TYPE_TIME should have been received instead."));
            [[fallthrough]];
        case SQL_TYPE_TIME:
        case SQL_SS_TIME2:
            returnCode = SqlDataBinder<SqlTime>::GetColumn(stmt, column, &variant.emplace<SqlTime>(), indicator, cb);
            break;
        case SQL_TYPE_TIMESTAMP:
            returnCode =
                SqlDataBinder<SqlDateTime>::GetColumn(stmt, column, &variant.emplace<SqlDateTime>(), indicator, cb);
            break;
        case SQL_TYPE_NULL:
            variant = SqlNullValue;
            returnCode = SQL_SUCCESS;
            break;
        case SQL_DECIMAL:
        case SQL_NUMERIC: {
            auto numeric = SQL_NUMERIC_STRUCT {};
            returnCode = SQLGetData(stmt, column, SQL_C_NUMERIC, &numeric, sizeof(numeric), indicator);

            if (SQL_SUCCEEDED(returnCode) && *indicator != SQL_NULL_DATA)
            {
                // clang-format off
                switch (numeric.scale)
                {
                    case 0: variant = static_cast<int64_t>(SqlNumeric<15, 0>(numeric).ToUnscaledValue()); break;
                    case 1: variant = SqlNumeric<15, 1>(numeric).ToFloat(); break;
                    case 2: variant = SqlNumeric<15, 2>(numeric).ToFloat(); break;
                    case 3: variant = SqlNumeric<15, 3>(numeric).ToFloat(); break;
                    case 4: variant = SqlNumeric<15, 4>(numeric).ToFloat(); break;
                    case 5: variant = SqlNumeric<15, 5>(numeric).ToFloat(); break;
                    case 6: variant = SqlNumeric<15, 6>(numeric).ToFloat(); break;
                    case 7: variant = SqlNumeric<15, 7>(numeric).ToFloat(); break;
                    case 8: variant = SqlNumeric<15, 8>(numeric).ToFloat(); break;
                    default: variant = SqlNumeric<15, 9>(numeric).ToFloat(); break;
                }
                // clang-format on
            }

            break;
        }
        case SQL_GUID:
            // TODO: Get them implemented on demand
            [[fallthrough]];
        default:
            SqlLogger::GetLogger().OnError(SqlError::UNSUPPORTED_TYPE);
            returnCode = SQL_ERROR; // std::errc::invalid_argument;
    }
    if (indicator && *indicator == SQL_NULL_DATA)
        variant = SqlNullValue;
    return returnCode;
}

std::string SqlVariant::ToString() const
{
    using namespace std::string_literals;

    // clang-format off
    return std::visit(detail::overloaded {
        [&](SqlNullType) { return "NULL"s; },
        [&](bool v) { return v ? "true"s : "false"s; },
        [&](int8_t v) { return std::to_string(v); },
        [&](short v) { return std::to_string(v); },
        [&](unsigned short v) { return std::to_string(v); },
        [&](int v) { return std::to_string(v); },
        [&](unsigned int v) { return std::to_string(v); },
        [&](long long v) { return std::to_string(v); },
        [&](unsigned long long v) { return std::to_string(v); },
        [&](float v) { return std::format("{}", v); },
        [&](double v) { return std::format("{}", v); },
        [&](std::string_view v) { return std::string(v); },
        [&](std::u16string_view v) {
            auto u8String = ToUtf8(v);
            auto stdString = std::string_view((char const*) u8String.data(), u8String.size());
            return std::format("{}", stdString);
        },
        [&](std::string const& v) { return v; },
        [&](SqlText const& v) { return v.value; },
        [&](SqlDate const& v) { return std::format("{}-{}-{}", v.sqlValue.year, v.sqlValue.month, v.sqlValue.day); },
        [&](SqlTime const& v) { return std::format("{}:{}:{}", v.sqlValue.hour, v.sqlValue.minute, v.sqlValue.second); },
        [&](SqlDateTime const& v) { return std::format("{}-{}-{} {}:{}:{}", v.sqlValue.year, v.sqlValue.month, v.sqlValue.day, v.sqlValue.hour, v.sqlValue.minute, v.sqlValue.second); }
        //[&](auto) { return "UNKNOWN"s; }
    }, value);
    // clang-format on
}
