// SPDX-License-Identifier: Apache-2.0

#pragma once

/// Export/import marker for the public symbols of the `Lightweight::Profiles` shared library.
/// The library is built with hidden default visibility, so only functions carrying this marker
/// are part of its export table.
#if defined(_MSC_VER)
    #if defined(BUILD_LIGHTWEIGHT_PROFILES)
        #define LIGHTWEIGHT_PROFILES_API __declspec(dllexport)
    #else
        #define LIGHTWEIGHT_PROFILES_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__)
    #define LIGHTWEIGHT_PROFILES_API __attribute__((visibility("default")))
#else
    #define LIGHTWEIGHT_PROFILES_API
#endif
