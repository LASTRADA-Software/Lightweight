// SPDX-License-Identifier: Apache-2.0

#pragma once

// This header exists so that every Lightweight header needing <sql.h>/<sqlext.h>/<sqlspi.h>/
// <sqltypes.h> on Windows can include *this* instead of the full <Windows.h> SDK aggregate.
//
// Investigation (see https://github.com/LASTRADA-Software/Lightweight/issues/547): the Windows
// ODBC headers are not self-contained. <sqltypes.h> unconditionally declares
// `typedef HWND SQLHWND;` on WIN32 (needed only so ODBC's connection-prompt APIs can carry an
// optional owner window — Lightweight always passes `(SQLHWND) nullptr`, see
// SqlConnection.cpp's SQLDriverConnectW call, and never dereferences it), and `typedef GUID
// SQLGUID;` guarded by GUID_DEFINED. <sqlext.h>'s legacy, Windows-8-removed Visual-Studio-tracing
// section (ODBC_VS_ARGS/FireVSDebugEvent) additionally references BOOL/DWORD/VOID/WCHAR/CHAR/
// LPWSTR. None of these are otherwise used by Lightweight (grep confirms: only prose comments and
// the library's own distinct SqlGuid type mention "GUID"); previously the full <Windows.h>
// prelude was pulled in just so the *parser* could get past these unconditional declarations.
//
// Below are the minimal, ABI-stable shapes the Windows SDK's own headers use internally for these
// symbols (HWND from <um/winuser.h>'s DECLARE_HANDLE expansion; GUID from <shared/guiddef.h>;
// the rest from <shared/minwindef.h>/<shared/winnt.h>). Defining them here — rather than
// including <Windows.h> — avoids pulling in the ~150k-line SDK aggregate (macro pollution beyond
// min/max, compile-time cost, and the ODR/ABI surface of everything Windows.h drags in) while
// still letting <sql.h> and friends parse.
//
// @warning These typedefs must stay byte-identical to the Windows SDK's own definitions: if a
// consumer's translation unit also includes <Windows.h> (directly or transitively) after this
// header, the two must be identical or the second one is a hard redefinition error. If a future
// Windows SDK version adds a new symbol reference to a deprecated/legacy section of <sql.h>/
// <sqlext.h>/<sqlspi.h>/<sqltypes.h> that isn't covered below, the fix is to extend this file with
// that symbol's own minimal shape — not to fall back to <Windows.h>.
#if defined(_WIN32) || defined(_WIN64)

    // Guards <Windows.h>'s own min/max function-like macros from leaking into every consumer TU
    // that includes this header (directly or transitively) and breaking any later std::min/
    // std::max/std::numeric_limits<T>::min()/max() call in the same TU (#542). These only take
    // effect if the consumer later includes the real <Windows.h> themselves; defined only if not
    // already picked, so an application-wide override still wins.
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

// HWND has no symbol-level include guard in the real SDK (only windef.h's own file guard), but
// a repeated identical forward-declaration + typedef is legal C++, so this is safe even if a
// consumer's TU later includes the real <Windows.h> too.
struct HWND__;
typedef struct HWND__* HWND; // NOLINT(modernize-use-using): mirrors <um/windef.h>'s own typedef.

    #ifndef GUID_DEFINED
        #define GUID_DEFINED
typedef struct _GUID // NOLINT(modernize-use-using): mirrors <shared/guiddef.h>'s own typedef.
{
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
    #endif

    #ifndef _MINWINDEF_SHIM_DEFINED
        #define _MINWINDEF_SHIM_DEFINED
using BOOL = int;
using DWORD = unsigned long;
using WCHAR = wchar_t;
using LPWSTR = WCHAR*;
    #endif

    // <um/winnt.h> gates VOID/CHAR/SHORT/LONG/INT as ONE atomic bundle behind a single
    // `#ifndef VOID` (not one guard per symbol): defining VOID alone here would make a later
    // real <Windows.h> skip the whole bundle's #ifndef body, silently leaving CHAR/SHORT/LONG/INT
    // undefined — which is exactly what broke winnt.h's PLONG-using interlocked-intrinsics section
    // (#547). Reproduce the full bundle atomically so a later winnt.h's #ifndef VOID correctly
    // finds everything already defined and skips cleanly, instead of partially pre-empting it.
    #ifndef VOID
        #define VOID void
using CHAR = char;
using SHORT = short;
using LONG = long;
        #if !defined(MIDL_PASS)
using INT = int;
        #endif
    #endif

    #include <basetsd.h>
    #include <sal.h>
    #include <winapifamily.h>
#endif
