// SPDX-License-Identifier: Apache-2.0

#include "BasicStringBinder.hpp"
#include "SqlBinary.hpp"
#include "SqlVariant.hpp"

namespace Lightweight
{

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
SQLRETURN SqlDataBinder<SqlVariant>::GetColumn(
    SQLHSTMT stmt, SQLUSMALLINT column, SqlVariant* result, SQLLEN* indicator, SqlDataBinderCallback const& cb) noexcept
{
    SQLLEN columnType {};
    // `SQL_DESC_CONCISE_TYPE` returns the concise SQL type code (e.g. SQL_TYPE_DATE,
    // SQL_TYPE_TIME, SQL_TYPE_TIMESTAMP). The verbose `SQL_DESC_TYPE` collapses every
    // datetime subtype to the single value `SQL_DATETIME` (9) and every interval
    // subtype to `SQL_INTERVAL` (10), so it cannot distinguish DATE from TIME.
    //
    // SQLColAttributeW is used (over the ANSI variant) to keep the ODBC call surface
    // on the Unicode-app path the rest of the library uses; the string buffer is
    // unused — only the numeric attribute is fetched.
    SQLRETURN returnCode = SQLColAttributeW(stmt, column, SQL_DESC_CONCISE_TYPE, nullptr, 0, nullptr, &columnType);
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
            // SQL_SMALLINT is a signed 16-bit integer. Fetching into `unsigned short` makes the
            // MSSQL ODBC driver report "Numeric value out of range" the first time a negative
            // value is fetched. Use `short` so the full signed range round-trips.
            returnCode = SqlDataBinder<short>::GetColumn(stmt, column, &variant.emplace<short>(), indicator, cb);
            break;
        case SQL_INTEGER:
            returnCode = SqlDataBinder<int>::GetColumn(stmt, column, &variant.emplace<int>(), indicator, cb);
            break;
        case SQL_BIGINT:
        // The SQLite ODBC driver reports `SQL_DESC_CONCISE_TYPE = SQL_C_SBIGINT (-25)` for
        // BIGINT columns instead of `SQL_BIGINT (-5)` — the C type leaks into the SQL type
        // slot. Treat it as the equivalent SQL type so SqlVariant round-trips BIGINT on SQLite.
        case SQL_C_SBIGINT:
            returnCode = SqlDataBinder<long long>::GetColumn(stmt, column, &variant.emplace<long long>(), indicator, cb);
            break;
        // Mirror the SQLite quirk for unsigned BIGINT — `SQL_C_UBIGINT (-27)` appearing as
        // a column type means the driver has tagged the value as unsigned 64-bit.
        case SQL_C_UBIGINT:
            returnCode = SqlDataBinder<unsigned long long>::GetColumn(
                stmt, column, &variant.emplace<unsigned long long>(), indicator, cb);
            break;
        case SQL_REAL:
            returnCode = SqlDataBinder<float>::GetColumn(stmt, column, &variant.emplace<float>(), indicator, cb);
            break;
        case SQL_FLOAT:
        case SQL_DOUBLE:
            returnCode = SqlDataBinder<double>::GetColumn(stmt, column, &variant.emplace<double>(), indicator, cb);
            break;
        case SQL_CHAR:        // fixed-length string
        case SQL_VARCHAR:     // variable-length string
        case SQL_LONGVARCHAR: // long string
            returnCode = SqlDataBinder<std::string>::GetColumn(stmt, column, &variant.emplace<std::string>(), indicator, cb);

            if (cb.ServerType() == SqlServerType::SQLITE && SQL_SUCCEEDED(returnCode))
            {
                // The SQLite driver returns SQL_VARCHAR for GUID columns.
                // So we need to have some heuristic to detect GUIDs in the string and convert them.
                if (auto maybeGuid = SqlGuid::TryParse(std::get<std::string>(variant)); maybeGuid)
                {
                    variant = maybeGuid.value();
                }
            }
            break;
        case SQL_WCHAR:        // fixed-length Unicode (UTF-16) string
        case SQL_WVARCHAR:     // variable-length Unicode (UTF-16) string
        case SQL_WLONGVARCHAR: // long Unicode (UTF-16) string
        {
            std::u16string u16str;
            returnCode = SqlDataBinder<std::u16string>::GetColumn(stmt, column, &u16str, indicator, cb);

            if (SQL_SUCCEEDED(returnCode))
            {
                // Convert UTF-16 to UTF-8 for consistent storage
                auto u8str = ToUtf8(std::u16string_view(u16str));
                std::string utf8str(reinterpret_cast<char const*>(u8str.data()), u8str.size());

                // Try to parse as GUID first (SQLite may return GUID columns as Unicode strings)
                if (cb.ServerType() == SqlServerType::SQLITE)
                {
                    if (auto maybeGuid = SqlGuid::TryParse(utf8str); maybeGuid)
                    {
                        variant = maybeGuid.value();
                        break;
                    }
                }

                // Store as std::string for easier handling
                variant = std::move(utf8str);
            }
            break;
        }
        case SQL_BINARY:        // fixed-length binary
        case SQL_VARBINARY:     // variable-length binary
        case SQL_LONGVARBINARY: // long binary
        {
            // Fetch via SQL_C_BINARY so drivers (MSSQL in particular) don't auto-convert the
            // byte payload to a hex ASCII string. The previous `SqlDataBinder<std::string>`
            // call asked for SQL_C_CHAR and got back "01AB23…" — double the byte count and
            // not the original bytes.
            SqlBinary binary;
            returnCode = SqlDataBinder<SqlBinary>::GetColumn(stmt, column, &binary, indicator, cb);
            if (SQL_SUCCEEDED(returnCode))
            {
                auto& bytes = variant.emplace<std::string>();
                bytes.assign(reinterpret_cast<char const*>(binary.data()), binary.size());

                if (cb.ServerType() == SqlServerType::SQLITE)
                {
                    // The SQLite driver may return GUID columns as binary data.
                    // Try to parse as GUID if the bytes look like one.
                    if (auto maybeGuid = SqlGuid::TryParse(bytes); maybeGuid)
                        variant = maybeGuid.value();
                }
            }
            break;
        }
        case SQL_DATE:
            // Oracle ODBC driver returns SQL_DATE for DATE columns
            returnCode = SqlDataBinder<SqlDateTime>::GetColumn(stmt, column, &variant.emplace<SqlDateTime>(), indicator, cb);
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
            returnCode = SqlDataBinder<SqlDateTime>::GetColumn(stmt, column, &variant.emplace<SqlDateTime>(), indicator, cb);
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
                    case 1: variant = SqlNumeric<15, 1>(numeric).ToDouble(); break;
                    case 2: variant = SqlNumeric<15, 2>(numeric).ToDouble(); break;
                    case 3: variant = SqlNumeric<15, 3>(numeric).ToDouble(); break;
                    case 4: variant = SqlNumeric<15, 4>(numeric).ToDouble(); break;
                    case 5: variant = SqlNumeric<15, 5>(numeric).ToDouble(); break;
                    case 6: variant = SqlNumeric<15, 6>(numeric).ToDouble(); break;
                    case 7: variant = SqlNumeric<15, 7>(numeric).ToDouble(); break;
                    case 8: variant = SqlNumeric<15, 8>(numeric).ToDouble(); break;
                    default: variant = SqlNumeric<15, 9>(numeric).ToDouble(); break;
                }
                // clang-format on
            }

            break;
        }
        case SQL_GUID:
            returnCode = SqlDataBinder<SqlGuid>::GetColumn(stmt, column, &variant.emplace<SqlGuid>(), indicator, cb);
            break;
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
        [&](SqlGuid guid) { return std::format("{}", guid); },
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
        [&](std::u16string const& v) {
            auto u8String = ToUtf8(std::u16string_view(v));
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

} // namespace Lightweight
