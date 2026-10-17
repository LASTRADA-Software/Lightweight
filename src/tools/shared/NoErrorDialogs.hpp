// SPDX-License-Identifier: Apache-2.0
//
// Keeps a process from opening modal Windows error dialogs (CRT asserts, abort(), invalid
// parameters, critical errors). Header-only, so test executables that link nothing from
// tools_shared can use it too. A no-op on other platforms.

#pragma once

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <cstdint>
    #include <cstdio>
    #include <cstdlib>
    #include <cwchar>
    #include <format>

    #include <Windows.h>
    #include <crtdbg.h>
#endif

namespace Lightweight::Tools
{

#if defined(_WIN32)
namespace detail
{
    /// Invalid-parameter handler: reports the call on stderr and aborts.
    inline void ReportInvalidParameterAndAbort(wchar_t const* expression,
                                               wchar_t const* function,
                                               wchar_t const* file,
                                               unsigned int line,
                                               std::uintptr_t /*reserved*/)
    {
        // Release CRTs pass null for all three texts and 0 for the line.
        auto const report = std::format(L"Invalid parameter: {} in {} ({}:{})\n",
                                        expression != nullptr ? expression : L"(expression unavailable)",
                                        function != nullptr ? function : L"(function unavailable)",
                                        file != nullptr ? file : L"(file unavailable)",
                                        line);
        std::fputws(report.c_str(), stderr);
        std::fflush(stderr);
        std::abort();
    }

    #if defined(_DEBUG)
    /// CRT debug report hook: writes the report to stderr; asserts and errors then abort.
    inline int __cdecl ReportToStderr(int reportType, char* message, int* returnValue)
    {
        std::fputs(message != nullptr ? message : "(no message)\n", stderr);
        std::fflush(stderr);
        if (reportType != _CRT_WARN)
            std::abort();
        if (returnValue != nullptr)
            *returnValue = 0;
        return TRUE;
    }

    /// Wide-character counterpart of `ReportToStderr`, used by `_ASSERTE` and `_RPTW*`.
    inline int __cdecl ReportToStderrWide(int reportType, wchar_t* message, int* returnValue)
    {
        std::fputws(message != nullptr ? message : L"(no message)\n", stderr);
        std::fflush(stderr);
        if (reportType != _CRT_WARN)
            std::abort();
        if (returnValue != nullptr)
            *returnValue = 0;
        return TRUE;
    }
    #endif
} // namespace detail
#endif

/// Routes every error the process would report in a modal Windows dialog to stderr instead:
/// CRT debug asserts and errors (which then abort), `abort()` itself, invalid parameters to
/// CRT functions (reported, then aborted), and critical errors such as a missing DLL or a
/// crash. The process still fails, with a non-zero exit code, but never waits for a click.
/// Call it first thing in `main`. Does nothing on other platforms.
inline void DisableErrorDialogs() noexcept
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(&detail::ReportInvalidParameterAndAbort);
    #if defined(_DEBUG)
    for (auto const reportType: { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
    {
        _CrtSetReportMode(reportType, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
    }
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &detail::ReportToStderr);
    _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, &detail::ReportToStderrWide);
    #endif
#endif
}

} // namespace Lightweight::Tools
