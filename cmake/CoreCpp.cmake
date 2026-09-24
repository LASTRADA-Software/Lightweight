# core-cpp (https://github.com/contour-terminal/core-cpp): dbtool draws its progress and help colours
# through core::tui_output, the part of core-cpp's terminal UI that composes escape sequences and
# links nothing but core::base. Included from the root CMakeLists.txt only when
# LIGHTWEIGHT_BUILD_TOOLS is ON, so a library-only configure fetches nothing of it.
#
# The full TUI is off and only its leaf is on, so core-cpp resolves no libunicode.
# CORE_CPP_WITH_TUI_OUTPUT is set explicitly because its default follows CORE_CPP_WITH_TUI.
# EXCLUDE_FROM_ALL keeps every target dbtool does not link out of the build.

# Lightweight's add_compile_options() calls -- the pedantic warning set with -Werror, /utf-8, the
# sanitizers -- are a directory property that a subdirectory inherits when it is added. core-cpp
# chooses its own warnings, so the property is cleared for the CPM call and restored after it.
get_directory_property(_lightweightCompileOptions COMPILE_OPTIONS)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

CPMAddPackage(
    NAME core-cpp
    GITHUB_REPOSITORY contour-terminal/core-cpp
    GIT_TAG v0.5.0
    VERSION 0.5.0
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
        "CORE_CPP_TESTING OFF"
        "CORE_CPP_BUILD_EXAMPLES OFF"
        "CORE_CPP_WITH_TUI OFF"
        "CORE_CPP_WITH_TUI_OUTPUT ON"
        "CORE_CPP_WITH_TLS OFF"
)

set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_lightweightCompileOptions}")
unset(_lightweightCompileOptions)

# The sanitizers were cleared with everything else above. core-cpp leaves instrumenting its targets
# to the parent, through the CORE_CPP_TARGETS global property, so that a sanitized dbtool does not
# link an uninstrumented core-cpp.
if(SANITIZER_COMPILE_OPTIONS)
    get_property(_lightweightCoreCppTargets GLOBAL PROPERTY CORE_CPP_TARGETS)
    foreach(_target IN LISTS _lightweightCoreCppTargets)
        target_compile_options(${_target} PRIVATE ${SANITIZER_COMPILE_OPTIONS})
    endforeach()
    unset(_target)
    unset(_lightweightCoreCppTargets)
endif()
