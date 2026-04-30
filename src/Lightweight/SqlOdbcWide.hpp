// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DataBinder/UnicodeConverter.hpp"

#include <string>
#include <string_view>
#include <utility>

#include <sql.h>

namespace Lightweight::detail
{

// ODBC W variants assume `SQLWCHAR` is layout-compatible with `char16_t` (true on
// Windows where it's `wchar_t`, and on Linux unixODBC where it's `unsigned short` —
// both 16-bit). Anything else and the reinterpret_casts below would silently corrupt.
static_assert(sizeof(SQLWCHAR) == sizeof(char16_t), "ODBC W variants require a 16-bit code unit; SQLWCHAR shape mismatch");

/// @brief Converts a UTF-8 string view into a `std::u16string` for ODBC W variants.
/// Use this rather than `ToUtf16(std::string const&)` from `UnicodeConverter.hpp`:
/// that overload treats its input as the platform narrow encoding (CP_ACP on
/// Windows), which silently corrupts UTF-8 bytes >= 0x80.
inline std::u16string OdbcUtf8ToUtf16(std::string_view utf8)
{
    return ToUtf16(std::u8string_view { reinterpret_cast<char8_t const*>(utf8.data()), utf8.size() });
}

/// @brief Reinterprets a `char16_t*` buffer as the `SQLWCHAR*` ODBC W variants want.
/// Note: ODBC W input-string parameters are non-const (a legacy API quirk; the
/// driver does not mutate them), so callers must hold their `std::u16string` non-const.
inline SQLWCHAR* AsSqlWChar(char16_t* p) noexcept
{
    return reinterpret_cast<SQLWCHAR*>(p);
}

/// @brief Holds a UTF-16 buffer mapped from a UTF-8 input plus the (pointer, length)
/// pair the ODBC W introspection / connect / prepare calls expect. Empty input
/// produces a null pointer / zero length, mirroring the ODBC idiom of "pass nullptr
/// when the caller does not want to filter on this field". The buffer's lifetime
/// equals the `OdbcWideArg`'s, satisfying SQL_NTS for the duration of one call.
struct OdbcWideArg
{
    std::u16string buffer;

    explicit OdbcWideArg(std::string_view utf8):
        buffer(utf8.empty() ? std::u16string {} : OdbcUtf8ToUtf16(utf8))
    {
    }

    explicit OdbcWideArg(std::u16string utf16) noexcept:
        buffer(std::move(utf16))
    {
    }

    [[nodiscard]] SQLWCHAR* data() noexcept
    {
        return buffer.empty() ? nullptr : AsSqlWChar(buffer.data());
    }

    [[nodiscard]] SQLSMALLINT length() const noexcept
    {
        return static_cast<SQLSMALLINT>(buffer.size());
    }
};

} // namespace Lightweight::detail
