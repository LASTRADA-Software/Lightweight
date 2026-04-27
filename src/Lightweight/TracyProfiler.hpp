// SPDX-License-Identifier: Apache-2.0

#pragma once

// Optional Tracy profiler instrumentation. Part of the Lightweight public API:
// any consumer (tests, tools, examples, downstream apps) inherits the macros.
//
// Configure with `-DLIGHTWEIGHT_ENABLE_TRACY=ON` (and `vcpkg install
// lightweight[tracy]` if using vcpkg) then connect the Tracy GUI to the
// running process. When the option is OFF, this header provides no-op stubs
// that mirror Tracy's macro names so call sites can use `ZoneScoped`,
// `ZoneScopedN(...)`, `ZoneText(...)`, `FrameMark`, etc. directly without
// changing the source between builds.

#if defined(LIGHTWEIGHT_TRACY_ENABLED)
    #include <tracy/Tracy.hpp>
#else
    // No-op stubs. Each macro consumes ALL of its arguments via `(void) (...)`
    // so call sites can pass locals (e.g. a sanitized SQL string, or a
    // `varname` introduced solely for a `*V` zone-named overload) without
    // triggering unused-variable / unused-parameter warnings when Tracy is
    // disabled.
    #define ZoneScoped
    #define ZoneScopedN(name)              ((void) sizeof(name))
    #define ZoneScopedC(color)             ((void) sizeof(color))
    #define ZoneScopedNC(name, color)      (((void) sizeof(name)), ((void) sizeof(color)))
    #define ZoneText(text, size)           (((void) (text)), ((void) (size)))
    #define ZoneTextV(varname, text, size) (((void) (varname)), ((void) (text)), ((void) (size)))
    #define ZoneName(text, size)           (((void) (text)), ((void) (size)))
    #define ZoneNameV(varname, text, size) (((void) (varname)), ((void) (text)), ((void) (size)))
    #define ZoneValue(value)               ((void) (value))
    #define ZoneValueV(varname, value)     (((void) (varname)), ((void) (value)))
    #define ZoneColor(color)               ((void) (color))
    #define ZoneColorV(varname, color)     (((void) (varname)), ((void) (color)))
    #define FrameMark
    #define FrameMarkNamed(name)   ((void) sizeof(name))
    #define FrameMarkStart(name)   ((void) sizeof(name))
    #define FrameMarkEnd(name)     ((void) sizeof(name))
    #define TracyPlot(name, value) (((void) sizeof(name)), ((void) (value)))
    #define TracyPlotConfig(name, type, step, fill, color) \
        (((void) sizeof(name)), ((void) (type)), ((void) (step)), ((void) (fill)), ((void) (color)))
    #define TracyMessage(text, size)         (((void) (text)), ((void) (size)))
    #define TracyMessageL(text)              ((void) (text))
    #define TracyMessageC(text, size, color) (((void) (text)), ((void) (size)), ((void) (color)))
    #define TracyMessageLC(text, color)      (((void) (text)), ((void) (color)))
    #define TracyAppInfo(text, size)         (((void) (text)), ((void) (size)))
#endif

// Convenience wrapper for the very common `ZoneText(s.data(), s.size())`
// pattern over std::string / std::string_view / SqlText / etc. Works in both
// enabled and disabled builds because it forwards to `ZoneText` above.
// NOTE: `obj` is evaluated twice — pass an lvalue, not a function call.
#define ZoneTextObject(obj) ZoneText((obj).data(), (obj).size())
