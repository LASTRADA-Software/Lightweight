// SPDX-License-Identifier: Apache-2.0

#include "DataBinder/UnicodeConverter.hpp"
#include "ImmortalInstance.hpp"
#include "SqlError.hpp"
#include "SqlLogger.hpp"
#include "SqlOdbcWide.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <map>
#include <mutex>
#include <string_view>

namespace Lightweight
{

SqlException::SqlException(SqlErrorInfo info, std::source_location /*sourceLocation*/):
    std::runtime_error(std::format("{}", info)),
    _info { std::move(info) }
{
    // Note: We don't log here because the exception will be thrown and handled by the caller.
    // Logging should happen at the point where errors are silently handled, not where they're thrown.
}

void SqlErrorInfo::RequireStatementSuccess(SQLRETURN result, SQLHSTMT hStmt, std::string_view message)
{
    if (result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO) [[likely]]
        return;

    throw std::runtime_error { std::format("{}: {}", message, FromStatementHandle(hStmt)) };
}

namespace
{
    // Held in a function-local static rather than a namespace-scope global so there is no mutable
    // global variable and no static-initialization order concern.
    //
    // Not thread-local: a test installs a source around a scoped operation, and the async layer may
    // run that operation on a pool thread, which a thread_local would not reach. That means install
    // and read genuinely happen on different threads, so the slot is atomic - a plain pointer here
    // would be a data race that TSan reports. Relaxed ordering suffices: the caller owns the
    // pointee's lifetime and no data is published through this pointer.
    std::atomic<SqlDiagnosticSource*>& DiagnosticSourceSlot() noexcept
    {
        static std::atomic<SqlDiagnosticSource*> slot { nullptr };
        return slot;
    }
} // namespace

void SetDiagnosticSource(SqlDiagnosticSource* source)
{
    DiagnosticSourceSlot().store(source, std::memory_order_relaxed);
}

SqlDiagnosticSource* GetDiagnosticSource() noexcept
{
    return DiagnosticSourceSlot().load(std::memory_order_relaxed);
}

namespace
{
    /// The handles of connections to ANSI-only drivers, each mapped to its connection handle.
    struct AnsiOnlyHandles
    {
        std::mutex mutex;
        std::map<SQLHANDLE, SQLHDBC> connectionOf;
        // Lets the common case, no ANSI-only connection in the process, skip the lock.
        std::atomic<std::size_t> count { 0 };
    };

    /// Never destroyed: statements and connections owned by other statics (the migration manager's
    /// data mapper, an application global) unregister their handles during static destruction.
    AnsiOnlyHandles& AnsiOnlyHandleRegistry() noexcept
    {
        return detail::ImmortalInstance<AnsiOnlyHandles>();
    }

    /// The number of bytes of the UTF-8 sequence that starts with @p lead.
    [[nodiscard]] constexpr std::size_t Utf8SequenceLength(unsigned char lead) noexcept
    {
        if (lead >= 0xF0U)
            return 4;
        if (lead >= 0xE0U)
            return 3;
        if (lead >= 0xC0U)
            return 2;
        return 1;
    }

    /// The length of @p bytes without a trailing UTF-8 sequence that @p bytes cuts short.
    [[nodiscard]] std::size_t CompleteUtf8Length(std::string_view bytes) noexcept
    {
        auto start = bytes.size();
        while (start > 0 && (static_cast<unsigned char>(bytes[start - 1]) & 0xC0U) == 0x80U)
            --start; // Continuation bytes of the last sequence.
        if (start == 0)
            return bytes.size();
        auto const lead = static_cast<unsigned char>(bytes[start - 1]);
        return bytes.size() - (start - 1) >= Utf8SequenceLength(lead) ? bytes.size() : start - 1;
    }

    /// Reads the first diagnostic record of @p handle through SQLGetDiagRec, for a driver that takes and
    /// returns text only through the ANSI entry points: its bytes are the driver's UTF-8, passed on
    /// unchanged, where the wide variant would widen each byte into a code unit of its own.
    SqlErrorInfo FromHandleNarrow(SQLSMALLINT handleType, SQLHANDLE handle) noexcept
    {
        SqlErrorInfo info {};

        constexpr SQLSMALLINT SqlStateLen = 6; // 5 SQLSTATE chars + NUL
        constexpr SQLSMALLINT MessageBufferLen = 1024;
        std::array<SQLCHAR, SqlStateLen> sqlStateBuffer {};
        std::array<SQLCHAR, MessageBufferLen> messageBuffer {};
        SQLSMALLINT messageLength {};

        auto const rc = SQLGetDiagRec(handleType,
                                      handle,
                                      1,
                                      sqlStateBuffer.data(),
                                      &info.nativeErrorCode,
                                      messageBuffer.data(),
                                      MessageBufferLen,
                                      &messageLength);
        if (!SQL_SUCCEEDED(rc))
            return info;

        try
        {
            info.sqlState.assign(reinterpret_cast<char const*>(sqlStateBuffer.data()), SqlStateLen - 1);
            auto const messageBytes = std::clamp(messageLength, SQLSMALLINT { 0 }, SQLSMALLINT { MessageBufferLen - 1 });
            auto const message =
                std::string_view { reinterpret_cast<char const*>(messageBuffer.data()), static_cast<size_t>(messageBytes) };
            // A message longer than the buffer arrives cut, possibly inside a character.
            info.message.assign(message.substr(0, CompleteUtf8Length(message)));
        }
        catch (...) // NOLINT(bugprone-empty-catch)
        {
            // Out of memory on the failure path: report what could be kept rather than throw.
        }
        return info;
    }
} // namespace

namespace detail
{
    void RegisterAnsiOnlyHandle(SQLHANDLE handle, SQLHDBC connection) noexcept
    {
        auto& registry = AnsiOnlyHandleRegistry();
        try
        {
            auto const lock = std::scoped_lock { registry.mutex };
            registry.connectionOf.insert_or_assign(handle, connection);
            registry.count.store(registry.connectionOf.size(), std::memory_order_relaxed);
        }
        catch (...) // NOLINT(bugprone-empty-catch)
        {
            // Unrecorded, the handle's diagnostics are read wide: garbled non-ASCII, nothing worse.
        }
    }

    void UnregisterAnsiOnlyHandle(SQLHANDLE handle) noexcept
    {
        auto& registry = AnsiOnlyHandleRegistry();
        if (registry.count.load(std::memory_order_relaxed) == 0)
            return;
        auto const lock = std::scoped_lock { registry.mutex };
        auto const entry = registry.connectionOf.find(handle);
        if (entry == registry.connectionOf.end())
            return;
        auto const isConnection = entry->second == handle;
        registry.connectionOf.erase(entry);
        // A connection also takes along the statements recorded under it, whose free a closed
        // connection skips.
        if (isConnection)
            std::erase_if(registry.connectionOf, [handle](auto const& other) { return other.second == handle; });
        registry.count.store(registry.connectionOf.size(), std::memory_order_relaxed);
    }

    bool IsAnsiOnlyHandle(SQLHANDLE handle) noexcept
    {
        auto& registry = AnsiOnlyHandleRegistry();
        if (registry.count.load(std::memory_order_relaxed) == 0)
            return false;
        auto const lock = std::scoped_lock { registry.mutex };
        return registry.connectionOf.contains(handle);
    }
} // namespace detail

// Collect ODBC diagnostics via the wide-character variant. Calling SQLGetDiagRecW (rather
// than the A variant) keeps us on the Unicode path the rest of the library uses, so
// driver-side diagnostic text containing non-ASCII characters round-trips without going
// through the system ANSI codepage - except for a handle of an ANSI-only driver, whose
// UTF-8 text the A variant passes on intact (see detail::RegisterAnsiOnlyHandle). This
// function is on the failure path itself and must never throw — encoding errors degrade
// to a best-effort lossy conversion.
SqlErrorInfo SqlErrorInfo::FromHandle(SQLSMALLINT handleType, SQLHANDLE handle)
{
    // Tests may substitute the driver as the source of diagnostics, so error paths can be driven
    // without provoking a real failure. Null in production, and only reached once a call has
    // already failed - the success path never gets here.
    if (auto* const source = GetDiagnosticSource())
        return source->Diagnose(handleType, handle);

    if (detail::IsAnsiOnlyHandle(handle))
        return FromHandleNarrow(handleType, handle);

    SqlErrorInfo info {};

    constexpr SQLSMALLINT SqlStateLen = 6; // 5 SQLSTATE chars + NUL
    constexpr SQLSMALLINT MessageBufferLen = 1024;
    std::array<SQLWCHAR, SqlStateLen> sqlStateBuffer {};
    std::array<SQLWCHAR, MessageBufferLen> messageBuffer {};
    SQLSMALLINT msgLen {};

    auto const rc = SQLGetDiagRecW(handleType,
                                   handle,
                                   1,
                                   sqlStateBuffer.data(),
                                   &info.nativeErrorCode,
                                   messageBuffer.data(),
                                   MessageBufferLen,
                                   &msgLen);
    if (!SQL_SUCCEEDED(rc))
        return info;

    // The SQLWCHAR ↔ char16_t layout invariant lives in SqlOdbcWide.hpp; the buffers
    // we just filled are reinterpretable as char16_t under that same assumption.
    auto const sqlStateChars = static_cast<size_t>(SqlStateLen - 1); // SQLSTATE is exactly 5 chars
    auto const sqlStateUtf8 =
        ToUtf8(std::u16string_view { reinterpret_cast<char16_t const*>(sqlStateBuffer.data()), sqlStateChars });
    info.sqlState.assign(reinterpret_cast<char const*>(sqlStateUtf8.data()), sqlStateUtf8.size());

    auto const messageChars =
        msgLen > 0 ? std::min(static_cast<SQLSMALLINT>(MessageBufferLen - 1), msgLen) : SQLSMALLINT { 0 };
    auto const messageUtf8 = ToUtf8(
        std::u16string_view { reinterpret_cast<char16_t const*>(messageBuffer.data()), static_cast<size_t>(messageChars) });
    info.message.assign(reinterpret_cast<char const*>(messageUtf8.data()), messageUtf8.size());

    return info;
}

} // namespace Lightweight
