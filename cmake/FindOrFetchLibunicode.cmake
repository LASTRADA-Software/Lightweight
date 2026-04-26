# SPDX-License-Identifier: Apache-2.0
#
# Locates libunicode — first via find_package, falling back to CPM/FetchContent
# from upstream. Defines the imported target `unicode::unicode` (or, on older
# packagings, `unicode::core`) that callers can link against.
#
# Pinned to the same version as endo's `tui` module so the vendored
# `MarkdownTable.cpp` keeps compiling against a known-good API.

set(LIBUNICODE_REQUIRED_VERSION "0.9.0")

if(NOT TARGET unicode::unicode AND NOT TARGET unicode::core)
    find_package(libunicode ${LIBUNICODE_REQUIRED_VERSION} QUIET)
endif()

if(TARGET unicode::unicode OR TARGET unicode::core)
    message(STATUS "libunicode: using system package")
else()
    message(STATUS "libunicode: not found, fetching v${LIBUNICODE_REQUIRED_VERSION} via CPM")
    CPMAddPackage(
        NAME libunicode
        GITHUB_REPOSITORY contour-terminal/libunicode
        GIT_TAG v${LIBUNICODE_REQUIRED_VERSION}
        OPTIONS
            "LIBUNICODE_TESTING OFF"
            "LIBUNICODE_BENCHMARK OFF"
            "LIBUNICODE_TOOLS OFF"
            "LIBUNICODE_EXAMPLES OFF"
            "BUILD_SHARED_LIBS OFF"
        EXCLUDE_FROM_ALL YES
        SYSTEM YES
    )
endif()
