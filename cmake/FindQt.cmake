# FindQt.cmake — Auto-detect Qt6 installation on Windows.
#
# Scans QT_ROOT_DIR (default: C:/Qt) for the newest Qt 6.x version,
# preferring msvc2022_64 over msvc2019_64. Appends the found cmake
# directory to CMAKE_PREFIX_PATH so that a subsequent find_package(Qt6 ...) succeeds.
#
# Handles stale caches gracefully: if Qt6_DIR points to a removed
# Qt version, the detection re-runs automatically.

set(QT_ROOT_DIR "C:/Qt" CACHE PATH "Root directory of Qt installations")

# Re-detect if Qt6_DIR is unset, NOTFOUND, or points to a removed directory.
if(NOT Qt6_DIR OR NOT EXISTS "${Qt6_DIR}")
    message(STATUS "[FindQt] Searching for Qt6 in ${QT_ROOT_DIR}...")

    # Strip stale Qt paths from CMAKE_PREFIX_PATH to prevent accumulation
    set(_qt_cleaned_prefix_path "")
    foreach(_path IN LISTS CMAKE_PREFIX_PATH)
        string(FIND "${_path}" "${QT_ROOT_DIR}/" _qt_path_match)
        if(_qt_path_match LESS 0)
            list(APPEND _qt_cleaned_prefix_path "${_path}")
        endif()
    endforeach()
    set(CMAKE_PREFIX_PATH "${_qt_cleaned_prefix_path}")

    if(EXISTS "${QT_ROOT_DIR}")
        file(GLOB _qt_versions RELATIVE "${QT_ROOT_DIR}" "${QT_ROOT_DIR}/6.*")
        list(SORT _qt_versions COMPARE NATURAL)
        list(REVERSE _qt_versions)

        foreach(_qt_ver IN LISTS _qt_versions)
            set(_qt_ver_path "${QT_ROOT_DIR}/${_qt_ver}")
            message(STATUS "[FindQt] Checking Qt version: ${_qt_ver}")
            if(IS_DIRECTORY "${_qt_ver_path}")
                foreach(_compiler IN ITEMS msvc2022_64 msvc2019_64)
                    set(_qt_cmake_path "${_qt_ver_path}/${_compiler}/lib/cmake")
                    if(EXISTS "${_qt_cmake_path}")
                        list(APPEND CMAKE_PREFIX_PATH "${_qt_cmake_path}")
                        message(STATUS "[FindQt] Found Qt ${_qt_ver} (${_compiler})")
                        set(_qt_found TRUE)
                        break()
                    endif()
                endforeach()
                if(_qt_found)
                    break()
                endif()
            endif()
        endforeach()
    else()
        message(STATUS "[FindQt] Qt root directory ${QT_ROOT_DIR} does not exist.")
    endif()

    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE STRING "" FORCE)
    unset(Qt6_DIR CACHE)
endif()
