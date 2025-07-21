// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(__GNUC__)
    #define LIGHTWEIGHT_NO_EXPORT    __attribute__((visibility("hidden")))
    #define LIGHTWEIGHT_EXPORT       __attribute__((visibility("default")))
    #define LIGHTWEIGHT_IMPORT       /*!*/
    #define LIGHTWEIGHT_FORCE_INLINE __attribute__((always_inline))
#elif defined(_MSC_VER)
    #define LIGHTWEIGHT_NO_EXPORT    /*!*/
    #define LIGHTWEIGHT_EXPORT       __declspec(dllexport)
    #define LIGHTWEIGHT_IMPORT       __declspec(dllimport)
    #define LIGHTWEIGHT_FORCE_INLINE __forceinline
#endif

#if defined(LIGHTWEIGHT_SHARED)
    #if defined(BUILD_LIGHTWEIGHT)
        #define LIGHTWEIGHT_API LIGHTWEIGHT_EXPORT
    #else
        #define LIGHTWEIGHT_API LIGHTWEIGHT_IMPORT
    #endif
#else
    #define LIGHTWEIGHT_API /*!*/
#endif

namespace Lightweight
{
}

namespace Light = Lightweight;
