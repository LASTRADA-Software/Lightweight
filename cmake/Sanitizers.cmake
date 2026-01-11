# Sanitizers.cmake - Clang/GCC sanitizer support
#
# Options:
#   ENABLE_SANITIZER_ADDRESS         - Enable AddressSanitizer (detects memory errors)
#   ENABLE_SANITIZER_UNDEFINED       - Enable UndefinedBehaviorSanitizer
#   ENABLE_SANITIZER_THREAD          - Enable ThreadSanitizer (detects data races)
#
# Note: ASan and TSan cannot be combined.

option(ENABLE_SANITIZER_ADDRESS "Enable AddressSanitizer" OFF)
option(ENABLE_SANITIZER_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_SANITIZER_THREAD "Enable ThreadSanitizer" OFF)

set(SANITIZER_COMPILE_OPTIONS "")
set(SANITIZER_LINK_OPTIONS "")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(SANITIZER_FLAGS "")

    if(ENABLE_SANITIZER_ADDRESS)
        list(APPEND SANITIZER_FLAGS "address")
    endif()

    if(ENABLE_SANITIZER_UNDEFINED)
        list(APPEND SANITIZER_FLAGS "undefined")
    endif()

    if(ENABLE_SANITIZER_THREAD)
        if(ENABLE_SANITIZER_ADDRESS)
            message(FATAL_ERROR "ThreadSanitizer cannot be combined with AddressSanitizer")
        endif()
        list(APPEND SANITIZER_FLAGS "thread")
    endif()

    if(SANITIZER_FLAGS)
        string(REPLACE ";" "," SANITIZER_FLAGS_STR "${SANITIZER_FLAGS}")
        message(STATUS "[Sanitizers] Enabling: ${SANITIZER_FLAGS_STR}")

        set(SANITIZER_COMPILE_OPTIONS
            -fsanitize=${SANITIZER_FLAGS_STR}
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
            CACHE INTERNAL "Sanitizer compile options"
        )
        set(SANITIZER_LINK_OPTIONS
            -fsanitize=${SANITIZER_FLAGS_STR}
            CACHE INTERNAL "Sanitizer link options"
        )

        if(ENABLE_SANITIZER_UNDEFINED)
            list(APPEND SANITIZER_COMPILE_OPTIONS -fno-sanitize-recover=undefined)
        endif()

        add_compile_options(${SANITIZER_COMPILE_OPTIONS})
        add_link_options(${SANITIZER_LINK_OPTIONS})
    else()
        message(STATUS "[Sanitizers] None enabled")
    endif()
else()
    message(STATUS "[Sanitizers] Sanitizers require Clang or GCC. Skipping.")
endif()
