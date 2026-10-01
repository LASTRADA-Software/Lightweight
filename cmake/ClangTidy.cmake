
option(ENABLE_TIDY "Enable clang-tidy [default: OFF]" OFF)

# Report findings in this project's own headers only. Anchored to the source root rather than a
# path shape, so neither the checkout's name nor a `src/` further up the path decides what is
# analysed, and fetched dependencies under the build tree never match.
set(LIGHTWEIGHT_CLANG_TIDY_HEADER_FILTER "--header-filter=^${CMAKE_SOURCE_DIR}/src/")
if(ENABLE_TIDY)
    find_program(CLANG_TIDY_EXE
        NAMES clang-tidy
        DOC "Path to clang-tidy executable")
    if(NOT CLANG_TIDY_EXE)
        message(STATUS "[clang-tidy] Not found.")
    else()
        message(STATUS "[clang-tidy] found: ${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};${LIGHTWEIGHT_CLANG_TIDY_HEADER_FILTER}")
    endif()
else()
    message(STATUS "[clang-tidy] Disabled.")
endif()
