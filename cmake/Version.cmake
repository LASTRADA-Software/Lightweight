#SPDX-License-Identifier: Apache-2.0
# CMake function to extract version triple and full version string from the source code repository.
#
# The following locations are checked in order:
# 1.) /version.txt file
# 2.) /.git directory with the output of `git describe ...`)
# 3.) /metainfo.xml with the first line's version number and optional (suffix) string
#
function(GetVersionInformation VersionTripleVar VersionStringVar)
    # Check if Git is available
    find_package(Git QUIET)

    if(EXISTS "${CMAKE_SOURCE_DIR}/version.txt")
        # 1.) /version.txt file
        file(READ "${CMAKE_SOURCE_DIR}/version.txt" version_text)
        string(STRIP "${version_text}" version_text)
        string(REGEX MATCH "^v?([0-9]*\\.[0-9]+\\.[0-9]+).*$" _ ${version_text})
        set(THE_VERSION ${CMAKE_MATCH_1})
        set(THE_VERSION_STRING "${version_text}")
        set(THE_SOURCE "${CMAKE_SOURCE_DIR}/version.txt")
    elseif(GIT_FOUND)
        # Try to get the latest annotated tag (e.g., v1.2.34)
        # --tags: prefers tags
        # --abbrev=0: only show the tag name, not the commit hash
        # --match "v*": only consider tags starting with 'v'
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --match "v*"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_RESULT
        )
        if (GIT_RESULT EQUAL 0 AND NOT "${GIT_TAG}" STREQUAL "")
            # Remove the 'v' prefix if it exists
            string(REGEX REPLACE "^v" "" VERSION_FROM_GIT "${GIT_TAG}")
            message(STATUS "Successfully retrieved version '${VERSION_FROM_GIT}' from Git tag.")
            set(THE_VERSION "${VERSION_FROM_GIT}")
            set(THE_VERSION_STRING "${VERSION_FROM_GIT}")
            set(THE_SOURCE "git")
        else()
            message(STATUS "Info: No suitable Git tag (e.g., 'v1.2.34') found.")
        endif()
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/.git")
        # 2.) .git directory with the output of `git describe ...`)
        execute_process(COMMAND git describe --all
            OUTPUT_VARIABLE git_branch
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "^(.*)\\/(.*)$$" _ "${git_branch}")
        set(THE_GIT_BRANCH "${CMAKE_MATCH_2}")
        message(STATUS "[Version] Git branch: ${THE_GIT_BRANCH}")

        execute_process(COMMAND git rev-parse --short HEAD
            OUTPUT_VARIABLE git_sha_short
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(THE_GIT_SHA_SHORT "${git_sha_short}")
        message(STATUS "[Version] Git SHA: ${THE_GIT_SHA_SHORT}")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/metainfo.xml")
        # 3.) /metainfo.xml with the first line's version number and optional (suffix) string
        file(READ "${CMAKE_SOURCE_DIR}/metainfo.xml" changelog_contents)
        # extract and construct version triple
        string(REGEX MATCH "<release version=\"([0-9]*\\.[0-9]+\\.[0-9]+)\".*$" _ "${changelog_contents}")
        set(THE_VERSION ${CMAKE_MATCH_1})

        # maybe append CI run-ID.
        if (NOT("$ENV{RUN_ID}" STREQUAL ""))
            string(CONCAT THE_VERSION "${THE_VERSION}." $ENV{RUN_ID})
        endif()

        set(THE_VERSION_STRING "${THE_VERSION}")
        set(THE_SOURCE "${CMAKE_SOURCE_DIR}/metainfo.xml")
    endif()

    if("${THE_VERSION}" STREQUAL "" OR "${THE_VERSION_STRING}" STREQUAL "")
        message(FATAL_ERROR "Cannot extract version information. No version.txt or .git directory or metainfo.xml found.")
    endif()

    message(STATUS "[Version] version source: ${THE_SOURCE}")
    message(STATUS "[Version] version triple: ${THE_VERSION}")
    message(STATUS "[Version] version string: ${THE_VERSION_STRING}")

    # Write resulting version triple and version string to parent scope's variables.
    set(${VersionTripleVar} "${THE_VERSION}" PARENT_SCOPE)
    set(${VersionStringVar} "${THE_VERSION_STRING}" PARENT_SCOPE)
endfunction()

# Converts a version such as 1.2.255 to 0x0102ff
# Gratefully taken from https://github.com/Cisco-Talos/clamav/blob/17c9f5b64f4a9a3fd624b1c9668d034d898a2534/cmake/Version.cmake
function(HexVersion version_hex_var major minor patch)
    math(EXPR version_dec "${major} * 256 * 256 + ${minor} * 256 + ${patch}")
    set(version_hex "0x")
    foreach(i RANGE 5 0 -1)
        math(EXPR num "(${version_dec} >> (4 * ${i})) & 15")
        string(SUBSTRING "0123456789abcdef" ${num} 1 num_hex)
        set(version_hex "${version_hex}${num_hex}")
    endforeach()
    set(${version_hex_var} "${version_hex}" PARENT_SCOPE)
endfunction()

# Converts a number such as 104 to 68
# Gratefully taken from https://github.com/Cisco-Talos/clamav/blob/17c9f5b64f4a9a3fd624b1c9668d034d898a2534/cmake/Version.cmake
function(NumberToHex number output)
    set(hex "")
    foreach(i RANGE 1)
        math(EXPR nibble "${number} & 15")
        string(SUBSTRING "0123456789abcdef" "${nibble}" 1 nibble_hex)
        string(APPEND hex "${nibble_hex}")
        math(EXPR number "${number} >> 4")
    endforeach()
    string(REGEX REPLACE "(.)(.)" "\\2\\1" hex "${hex}")
    set("${output}" "${hex}" PARENT_SCOPE)
endfunction()
