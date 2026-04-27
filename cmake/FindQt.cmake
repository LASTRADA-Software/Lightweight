# FindQt.cmake
#
# Helper that locates a Qt 6 installation when the user has not explicitly
# pointed CMake at one. The goal is to make `cmake -DLIGHTWEIGHT_BUILD_GUI=ON`
# work out-of-the-box when Qt is installed in a standard location, while still
# honouring explicit configuration (Qt6_DIR, CMAKE_PREFIX_PATH, QT_ROOT_DIR).
#
# NOTE: despite the Find*.cmake naming, this is a helper module that exposes
# the `lightweight_probe_qt6()` function — it is not intended to be driven by
# `find_package(Qt)` and does not set the usual `Qt_FOUND` contract. Include
# it explicitly via `include(FindQt)` and then call `lightweight_probe_qt6()`.
#
# Resolution order:
#   1. If Qt6_DIR is already set in the cache -> keep it, do nothing.
#   2. If CMAKE_PREFIX_PATH already resolves Qt6 -> keep it, do nothing.
#   3. Probe well-known install roots for Qt 6.5+ (LTS and newer).
#   4. On failure, report what was searched and let the caller decide whether
#      to fall back to LIGHTWEIGHT_BUILD_GUI=OFF.
#
# After a successful probe, CMAKE_PREFIX_PATH is extended and the caller can
# simply do `find_package(Qt6 ... COMPONENTS ...)`.
#
# Public entry point:
#   lightweight_probe_qt6([REQUIRED_COMPONENTS <comp> ...]
#                        [MIN_VERSION <ver>]
#                        [RESULT_VAR <var>])
#
#   RESULT_VAR, if given, is set to TRUE on success and FALSE on failure.

include_guard(GLOBAL)

function(_lw_qt_candidate_roots out_var)
    set(roots "")

    # Windows: Qt Online Installer default is C:/Qt, occasionally D:/Qt or on
    # the system drive. Also respect $USERPROFILE/Qt for per-user installs.
    if(WIN32)
        list(APPEND roots "C:/Qt" "D:/Qt")
        if(DEFINED ENV{SystemDrive})
            list(APPEND roots "$ENV{SystemDrive}/Qt")
        endif()
        if(DEFINED ENV{USERPROFILE})
            file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Qt" _up_qt)
            list(APPEND roots "${_up_qt}")
        endif()
    endif()

    # macOS: Homebrew keg-only qt@6 (both Apple Silicon and Intel prefixes).
    if(APPLE)
        list(APPEND roots
            "/opt/homebrew/opt/qt@6"
            "/opt/homebrew/opt/qt"
            "/usr/local/opt/qt@6"
            "/usr/local/opt/qt"
        )
        if(DEFINED ENV{HOME})
            list(APPEND roots "$ENV{HOME}/Qt")
        endif()
    endif()

    # Linux: mostly handled by the distro-provided Qt6Config.cmake, but honour
    # a user-local Qt Online Installer layout under $HOME/Qt as a fallback.
    if(UNIX AND NOT APPLE)
        if(DEFINED ENV{HOME})
            list(APPEND roots "$ENV{HOME}/Qt")
        endif()
        list(APPEND roots "/opt/Qt")
    endif()

    # Also honour QT_ROOT_DIR env var if set (Qt Online Installer sometimes
    # exports this in its "Qt Creator"-provided shell).
    if(DEFINED ENV{QT_ROOT_DIR})
        list(APPEND roots "$ENV{QT_ROOT_DIR}")
    endif()

    list(REMOVE_DUPLICATES roots)
    set(${out_var} "${roots}" PARENT_SCOPE)
endfunction()

# Given a Qt install root (e.g. C:/Qt), return candidate cmake directories
# (.../lib/cmake/Qt6) sorted newest-version-first.
function(_lw_qt_cmake_dirs_under_root root min_version out_var)
    set(candidates "")

    if(NOT IS_DIRECTORY "${root}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    # A Qt Online Installer layout looks like:
    #   C:/Qt/6.9.0/msvc2022_64/lib/cmake/Qt6
    #   C:/Qt/6.5.3/mingw_64/lib/cmake/Qt6
    # A Homebrew layout looks like:
    #   /opt/homebrew/opt/qt@6/lib/cmake/Qt6
    # A distro layout looks like:
    #   /usr/lib/x86_64-linux-gnu/cmake/Qt6  (already handled by system pkg)
    #
    # First, check if root itself is a Qt prefix.
    if(EXISTS "${root}/lib/cmake/Qt6/Qt6Config.cmake")
        list(APPEND candidates "${root}/lib/cmake/Qt6")
    endif()

    # Then scan for version subdirectories (Qt Online Installer layout).
    file(GLOB _version_dirs RELATIVE "${root}" "${root}/6.*")
    # Sort descending so newer versions are tried first.
    list(SORT _version_dirs COMPARE NATURAL ORDER DESCENDING)

    foreach(vdir IN LISTS _version_dirs)
        set(vroot "${root}/${vdir}")
        if(NOT IS_DIRECTORY "${vroot}")
            continue()
        endif()

        # Skip versions below the required minimum.
        if(min_version AND vdir VERSION_LESS min_version)
            continue()
        endif()

        # Inside a version dir, compiler-specific subdirs hold the actual Qt
        # prefix (msvc2022_64, mingw_1200_64, gcc_64, macos, etc.). Prefer
        # 64-bit MSVC on Windows, then MinGW; prefer gcc_64 on Linux; prefer
        # macos on macOS.
        set(_compiler_candidates "")
        if(WIN32)
            list(APPEND _compiler_candidates
                "msvc2022_64" "msvc2019_64" "msvc2022_arm64"
                "mingw_1310_64" "mingw_1200_64" "mingw_1120_64" "mingw_64"
                "llvm-mingw_64"
            )
        elseif(APPLE)
            list(APPEND _compiler_candidates "macos" "clang_64")
        else()
            list(APPEND _compiler_candidates "gcc_64" "linux_gcc_64")
        endif()

        foreach(comp IN LISTS _compiler_candidates)
            set(cmake_dir "${vroot}/${comp}/lib/cmake/Qt6")
            if(EXISTS "${cmake_dir}/Qt6Config.cmake")
                list(APPEND candidates "${cmake_dir}")
            endif()
        endforeach()

        # Fallback: pick whichever compiler subdir exists with a Qt6Config.
        file(GLOB _comp_subdirs RELATIVE "${vroot}" "${vroot}/*")
        foreach(comp IN LISTS _comp_subdirs)
            set(cmake_dir "${vroot}/${comp}/lib/cmake/Qt6")
            if(EXISTS "${cmake_dir}/Qt6Config.cmake")
                list(APPEND candidates "${cmake_dir}")
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES candidates)
    set(${out_var} "${candidates}" PARENT_SCOPE)
endfunction()

function(lightweight_probe_qt6)
    set(options "")
    set(oneValueArgs MIN_VERSION RESULT_VAR)
    set(multiValueArgs REQUIRED_COMPONENTS)
    cmake_parse_arguments(LW_QT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT LW_QT_MIN_VERSION)
        set(LW_QT_MIN_VERSION "6.5.0")
    endif()

    # Fast path: caller already pointed at Qt explicitly.
    if(DEFINED CACHE{Qt6_DIR} AND EXISTS "${Qt6_DIR}/Qt6Config.cmake")
        message(STATUS "Qt6 already configured via Qt6_DIR=${Qt6_DIR}, skipping probe.")
        if(LW_QT_RESULT_VAR)
            set(${LW_QT_RESULT_VAR} TRUE PARENT_SCOPE)
        endif()
        return()
    endif()

    # Also fast-path if CMAKE_PREFIX_PATH / system package already resolves it.
    find_package(Qt6 ${LW_QT_MIN_VERSION} QUIET COMPONENTS Core)
    if(Qt6_FOUND)
        message(STATUS "Qt6 ${Qt6_VERSION} found via existing CMAKE_PREFIX_PATH / system config.")
        if(LW_QT_RESULT_VAR)
            set(${LW_QT_RESULT_VAR} TRUE PARENT_SCOPE)
        endif()
        return()
    endif()

    _lw_qt_candidate_roots(_roots)

    set(_probed "")
    set(_picked "")
    foreach(root IN LISTS _roots)
        _lw_qt_cmake_dirs_under_root("${root}" "${LW_QT_MIN_VERSION}" _dirs)
        foreach(d IN LISTS _dirs)
            list(APPEND _probed "${d}")
            if(NOT _picked)
                set(_picked "${d}")
            endif()
        endforeach()
    endforeach()

    if(_picked)
        get_filename_component(_qt_prefix "${_picked}/../../.." ABSOLUTE)
        message(STATUS "Qt6 auto-detected at: ${_qt_prefix}")
        list(PREPEND CMAKE_PREFIX_PATH "${_qt_prefix}")
        set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
        set(Qt6_DIR "${_picked}" CACHE PATH "Qt6 cmake config directory" FORCE)
        if(LW_QT_RESULT_VAR)
            set(${LW_QT_RESULT_VAR} TRUE PARENT_SCOPE)
        endif()
        return()
    endif()

    # Nothing found. Report clearly so the user can fix their install or pass
    # -DQt6_DIR=... / -DCMAKE_PREFIX_PATH=... explicitly.
    message(STATUS "Qt6 auto-detection failed. Searched roots:")
    foreach(r IN LISTS _roots)
        message(STATUS "    ${r}")
    endforeach()
    if(LW_QT_RESULT_VAR)
        set(${LW_QT_RESULT_VAR} FALSE PARENT_SCOPE)
    endif()
endfunction()
