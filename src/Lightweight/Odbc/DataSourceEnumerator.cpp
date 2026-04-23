// SPDX-License-Identifier: Apache-2.0

#include "DataSourceEnumerator.hpp"

#if defined(_WIN32) || defined(_WIN64)
    #include <Windows.h> // NOLINT(llvm-include-order) — must precede sqlext.h on Windows.
#endif

#include <sql.h>
#include <sqlext.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>

namespace Lightweight::Odbc
{

namespace
{

/// RAII wrapper around a scratch `SQL_HANDLE_ENV` used only for enumeration.
///
/// A fresh env handle per call keeps us decoupled from `SqlConnection`'s
/// per-connection env — this matters for the migrations GUI, which may want
/// to refresh the DSN list while a connection is open against a different
/// driver manager configuration.
class ScratchEnv
{
  public:
    ScratchEnv() noexcept
    {
        if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &_hEnv) != SQL_SUCCESS)
        {
            _hEnv = SQL_NULL_HANDLE;
            return;
        }
        // ODBC 3.x is a prerequisite for the driver manager to honour the
        // `SQL_FETCH_*` modes used below. Cast matches Microsoft's header
        // expectations (integer-as-pointer for environment attributes).
        SQLSetEnvAttr(
            _hEnv, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0); // NOLINT(performance-no-int-to-ptr)
    }

    ~ScratchEnv()
    {
        if (_hEnv != SQL_NULL_HANDLE)
            SQLFreeHandle(SQL_HANDLE_ENV, _hEnv);
    }

    ScratchEnv(ScratchEnv const&) = delete;
    ScratchEnv(ScratchEnv&&) = delete;
    ScratchEnv& operator=(ScratchEnv const&) = delete;
    ScratchEnv& operator=(ScratchEnv&&) = delete;

    [[nodiscard]] SQLHENV Handle() const noexcept
    {
        return _hEnv;
    }
    [[nodiscard]] bool Ok() const noexcept
    {
        return _hEnv != SQL_NULL_HANDLE;
    }

  private:
    SQLHENV _hEnv = SQL_NULL_HANDLE;
};

/// Trims trailing NULs / whitespace that some driver managers include when
/// reporting fixed-width DSN fields.
[[nodiscard]] std::string NormalizeBuffer(std::span<SQLCHAR const> buffer) noexcept
{
    std::string_view const view { reinterpret_cast<char const*>(buffer.data()), buffer.size() };
    auto const firstNul = view.find('\0');
    auto const bounded = firstNul == std::string_view::npos ? view : view.substr(0, firstNul);

    auto const last = bounded.find_last_not_of(" \t\r\n");
    if (last == std::string_view::npos)
        return {};
    return std::string { bounded.substr(0, last + 1) };
}

/// Parses a driver attributes buffer — a double-NUL-terminated sequence of
/// `KEY=VALUE\0KEY=VALUE\0\0` entries — into a vector of key/value pairs.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> ParseDriverAttributes(
    std::span<SQLCHAR const> buffer) noexcept
{
    std::vector<std::pair<std::string, std::string>> attributes;

    char const* cursor = reinterpret_cast<char const*>(buffer.data());
    char const* const end = cursor + buffer.size();

    while (cursor < end && *cursor != '\0')
    {
        std::string_view const entry { cursor, std::strlen(cursor) };
        if (auto const eq = entry.find('='); eq != std::string_view::npos)
            attributes.emplace_back(std::string { entry.substr(0, eq) }, std::string { entry.substr(eq + 1) });
        else
            attributes.emplace_back(std::string { entry }, std::string {});
        cursor += entry.size() + 1;
    }

    return attributes;
}

/// Runs a single `SQLDataSources` scan pass (user or system) and appends its
/// results to `out`. Stops on the first non-success / non-info return code,
/// which matches the driver manager's end-of-iteration contract.
void EnumerateDataSourcesPass(SQLHENV hEnv, SQLUSMALLINT fetchType, DataSourceInfo::Scope scope,
                              std::vector<DataSourceInfo>& out)
{
    constexpr SQLSMALLINT NameBufferSize = SQL_MAX_DSN_LENGTH + 1;
    constexpr SQLSMALLINT DescriptionBufferSize = 256;

    std::array<SQLCHAR, NameBufferSize> nameBuf {};
    std::array<SQLCHAR, DescriptionBufferSize> descBuf {};
    SQLSMALLINT nameLen = 0;
    SQLSMALLINT descLen = 0;

    SQLRETURN rc = SQLDataSources(hEnv,
                                  fetchType,
                                  nameBuf.data(),
                                  static_cast<SQLSMALLINT>(nameBuf.size()),
                                  &nameLen,
                                  descBuf.data(),
                                  static_cast<SQLSMALLINT>(descBuf.size()),
                                  &descLen);
    while (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)
    {
        out.push_back(DataSourceInfo {
            .name = NormalizeBuffer(std::span<SQLCHAR const> { nameBuf.data(), static_cast<size_t>(nameLen) }),
            .description = NormalizeBuffer(std::span<SQLCHAR const> { descBuf.data(), static_cast<size_t>(descLen) }),
            .scope = scope,
        });

        // Every pass after the first on the same fetchType wants FETCH_NEXT.
        rc = SQLDataSources(hEnv,
                            SQL_FETCH_NEXT,
                            nameBuf.data(),
                            static_cast<SQLSMALLINT>(nameBuf.size()),
                            &nameLen,
                            descBuf.data(),
                            static_cast<SQLSMALLINT>(descBuf.size()),
                            &descLen);
    }
}

} // namespace

std::vector<DataSourceInfo> EnumerateDataSources()
{
    ScratchEnv env;
    if (!env.Ok())
        return {};

    std::vector<DataSourceInfo> result;
    EnumerateDataSourcesPass(env.Handle(), SQL_FETCH_FIRST_USER, DataSourceInfo::Scope::User, result);
    EnumerateDataSourcesPass(env.Handle(), SQL_FETCH_FIRST_SYSTEM, DataSourceInfo::Scope::System, result);
    return result;
}

std::vector<DriverInfo> EnumerateDrivers()
{
    ScratchEnv env;
    if (!env.Ok())
        return {};

    // Driver description and attribute buffers may be large for drivers with
    // many reported attributes (e.g. "ODBC Driver 18 for SQL Server"). 1 KiB
    // covers every driver I have seen in the wild; truncation is benign (the
    // attributes are informational).
    constexpr SQLSMALLINT DescriptionBufferSize = 512;
    constexpr SQLSMALLINT AttributeBufferSize = 1024;

    std::array<SQLCHAR, DescriptionBufferSize> descBuf {};
    std::array<SQLCHAR, AttributeBufferSize> attrBuf {};
    SQLSMALLINT descLen = 0;
    SQLSMALLINT attrLen = 0;

    std::vector<DriverInfo> result;

    SQLRETURN rc = SQLDrivers(env.Handle(),
                              SQL_FETCH_FIRST,
                              descBuf.data(),
                              static_cast<SQLSMALLINT>(descBuf.size()),
                              &descLen,
                              attrBuf.data(),
                              static_cast<SQLSMALLINT>(attrBuf.size()),
                              &attrLen);
    while (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)
    {
        result.push_back(DriverInfo {
            .name = NormalizeBuffer(std::span<SQLCHAR const> { descBuf.data(), static_cast<size_t>(descLen) }),
            .attributes = ParseDriverAttributes(std::span<SQLCHAR const> { attrBuf.data(), static_cast<size_t>(attrLen) }),
        });

        rc = SQLDrivers(env.Handle(),
                        SQL_FETCH_NEXT,
                        descBuf.data(),
                        static_cast<SQLSMALLINT>(descBuf.size()),
                        &descLen,
                        attrBuf.data(),
                        static_cast<SQLSMALLINT>(attrBuf.size()),
                        &attrLen);
    }

    return result;
}

} // namespace Lightweight::Odbc
