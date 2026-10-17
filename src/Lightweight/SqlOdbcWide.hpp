// SPDX-License-Identifier: Apache-2.0

#pragma once

// <sql.h> below needs a few symbols resolved first — see SqlOdbcPrelude.hpp's header comment.
#include "DataBinder/UnicodeConverter.hpp"
#include "SqlOdbcPrelude.hpp"

#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
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

/// @brief Holds a UTF-8 string as the (pointer, length) pair an ODBC ANSI entry point expects, for a
/// driver that takes text only through those (see SqlConnection::AcceptsWideQueryText()). The bytes are
/// passed on unchanged; empty input produces a null pointer / zero length, as with @ref OdbcWideArg.
struct OdbcNarrowArg
{
    std::string buffer;

    explicit OdbcNarrowArg(std::string_view utf8):
        buffer(utf8)
    {
    }

    [[nodiscard]] SQLCHAR* data() noexcept
    {
        return buffer.empty() ? nullptr : reinterpret_cast<SQLCHAR*>(buffer.data());
    }

    [[nodiscard]] SQLSMALLINT length() const noexcept
    {
        return static_cast<SQLSMALLINT>(buffer.size());
    }
};

/// @brief Reads the text column @p column of @p cursor as UTF-8.
///
/// Catalog and metadata text is fetched as UTF-16 and converted, because a narrow read hands back
/// whatever the driver chooses for SQL_C_CHAR: the Microsoft SQL Server driver on Windows, for one,
/// returns a non-ASCII identifier in the client code page rather than UTF-8.
/// @param cursor A result cursor (or statement) positioned on a row.
/// @param column The 1-based column.
/// @return The text, or nothing for NULL.
template <typename Cursor>
std::optional<std::string> GetNullableTextColumn(Cursor const& cursor, SQLUSMALLINT column)
{
    auto const text = cursor.template GetNullableColumn<std::u16string>(column);
    if (!text)
        return std::nullopt;
    auto const utf8 = ToUtf8(std::u16string_view { *text });
    return std::string { reinterpret_cast<char const*>(utf8.data()), utf8.size() };
}

/// @brief Reads the text column @p column of @p cursor as UTF-8, a NULL as the empty string.
template <typename Cursor>
std::string GetTextColumn(Cursor const& cursor, SQLUSMALLINT column)
{
    return GetNullableTextColumn(cursor, column).value_or(std::string {});
}

/// @brief Records that @p handle belongs to the connection @p connection, whose driver takes text only
/// through the ODBC ANSI entry points, so that SqlErrorInfo::FromHandle() reads the handle's diagnostics
/// through those as well. Best effort: a handle that cannot be recorded is read wide.
/// @param handle A connection handle, or a statement handle allocated on @p connection.
/// @param connection The connection handle; @p handle itself for a connection.
void RegisterAnsiOnlyHandle(SQLHANDLE handle, SQLHDBC connection) noexcept;

/// @brief Forgets @p handle, and for a connection also every handle recorded under it, before the
/// handle is freed.
void UnregisterAnsiOnlyHandle(SQLHANDLE handle) noexcept;

/// @brief Tells whether @p handle was recorded by RegisterAnsiOnlyHandle().
[[nodiscard]] bool IsAnsiOnlyHandle(SQLHANDLE handle) noexcept;

/// @brief Calls @p call with one argument per string of @p texts: an @ref OdbcWideArg each when @p wide,
/// else an @ref OdbcNarrowArg each, so one call site serves both the W and the ANSI entry point.
/// @return What @p call returned.
template <typename Call, typename... Texts>
decltype(auto) CallWithOdbcText(bool wide, Call const& call, Texts const&... texts)
{
    if (wide)
    {
        auto args = std::tuple { OdbcWideArg { std::string_view { texts } }... };
        return std::apply(call, args);
    }
    auto args = std::tuple { OdbcNarrowArg { std::string_view { texts } }... };
    return std::apply(call, args);
}

/// @brief Selects @p wideFunction for an @ref OdbcWideArg argument and @p ansiFunction for an
/// @ref OdbcNarrowArg one, to pair with @ref CallWithOdbcText.
template <typename Arg, typename WideFunction, typename AnsiFunction>
[[nodiscard]] constexpr auto OdbcEntryPoint(WideFunction* wideFunction, AnsiFunction* ansiFunction) noexcept
{
    if constexpr (std::same_as<std::remove_cvref_t<Arg>, OdbcWideArg>)
        return wideFunction;
    else
        return ansiFunction;
}

} // namespace Lightweight::detail
