# SPDX-License-Identifier: Apache-2.0
#
# Script-mode test: the installed Lightweight package must provide Lightweight::Profiles, and a
# standalone project linking only that target must build and run. Run with:
#   cmake -DBUILD_DIR=<build tree> -DSOURCE_DIR=<repo root> [-DGENERATOR=..] [-DTOOLCHAIN_FILE=..]
#         [-DTARGET_TRIPLET=..] [-DPREFIX_PATH=a|b] [-DBUILD_TYPE=..] [-DCXX_COMPILER=..]
#         [-DMAKE_PROGRAM=..] [-DLIBDIR=..] -P cmake/tests/ProfilesPackageTest.cmake
# Registered as the `ProfilesPackageTest` CTest; the toolchain settings are forwarded from the
# parent build so the consumer is configured exactly like the library was.

cmake_minimum_required(VERSION 3.25)

if(NOT LIBDIR)
    set(LIBDIR lib)
endif()

foreach(_required BUILD_DIR SOURCE_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "ProfilesPackageTest: pass -D${_required}=<path>")
    endif()
endforeach()

set(_prefix "${BUILD_DIR}/profiles-pkg-test/prefix")
set(_consumer_build "${BUILD_DIR}/profiles-pkg-test/consumer-build")
set(_user_config "${BUILD_DIR}/profiles-pkg-test/user-config")

file(REMOVE_RECURSE "${BUILD_DIR}/profiles-pkg-test")
file(MAKE_DIRECTORY "${_user_config}")

function(run_step description)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "ProfilesPackageTest: ${description} failed (${_rc})\n${_out}\n${_err}")
    endif()
endfunction()

# 1. Install the current build into a fresh prefix.
set(_config_args "")
if(BUILD_TYPE)
    set(_config_args --config "${BUILD_TYPE}")
endif()
foreach(_component Development Runtime)
    run_step("install (${_component})"
        "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${_prefix}" --component ${_component} ${_config_args})
endforeach()

# 2. Configure and build the consumer against the installed package, like the parent build.
# Seed the consumer's cache from a script: a ';'-separated CMAKE_PREFIX_PATH cannot survive being
# passed through a command line.
set(_preload "${BUILD_DIR}/profiles-pkg-test/preload.cmake")
set(_prefix_path "${_prefix}")
if(PREFIX_PATH)
    string(REPLACE "|" ";" PREFIX_PATH "${PREFIX_PATH}")
    list(APPEND _prefix_path ${PREFIX_PATH})
endif()
file(WRITE "${_preload}" "set(CMAKE_PREFIX_PATH \"${_prefix_path}\" CACHE STRING \"\")\n")
set(_forward -C "${_preload}")
foreach(_pair CMAKE_TOOLCHAIN_FILE:TOOLCHAIN_FILE VCPKG_TARGET_TRIPLET:TARGET_TRIPLET
              CMAKE_BUILD_TYPE:BUILD_TYPE CMAKE_CXX_COMPILER:CXX_COMPILER CMAKE_MAKE_PROGRAM:MAKE_PROGRAM)
    string(REPLACE ":" ";" _pair_parts "${_pair}")
    list(GET _pair_parts 0 _name)
    list(GET _pair_parts 1 _var)
    if(${_var})
        list(APPEND _forward "-D${_name}=${${_var}}")
    endif()
endforeach()
if(GENERATOR)
    list(APPEND _forward -G "${GENERATOR}")
endif()

run_step("consumer configure"
    "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/src/tests/profiles_consumer" -B "${_consumer_build}" ${_forward})
run_step("consumer build"
    "${CMAKE_COMMAND}" --build "${_consumer_build}" ${_config_args})

# 3. Run it with an empty per-user config dir and the installed libraries on the search path.
find_program(_consumer profiles_consumer PATHS "${_consumer_build}" "${_consumer_build}/${BUILD_TYPE}"
    NO_DEFAULT_PATH REQUIRED)
if(WIN32)
    set(_runtime_var PATH)
    set(_runtime_dir "${_prefix}/bin")
elseif(APPLE)
    set(_runtime_var DYLD_LIBRARY_PATH)
    set(_runtime_dir "${_prefix}/${LIBDIR}")
else()
    set(_runtime_var LD_LIBRARY_PATH)
    set(_runtime_dir "${_prefix}/${LIBDIR}")
endif()
set(ENV{LIGHTWEIGHT_DBTOOL_USER_CONFIG_DIR} "${_user_config}")
set(_search_path "${_runtime_dir}")
if(PREFIX_PATH)
    # vcpkg-provided DLLs/shared objects that were not bundled into the install tree.
    foreach(_dir IN LISTS PREFIX_PATH)
        if(WIN32 AND EXISTS "${_dir}/bin")
            list(APPEND _search_path "${_dir}/bin")
        elseif(EXISTS "${_dir}/lib")
            list(APPEND _search_path "${_dir}/lib")
        endif()
    endforeach()
endif()
if(WIN32)
    list(JOIN _search_path ";" _joined)
    set(ENV{PATH} "${_joined};$ENV{PATH}")
else()
    list(JOIN _search_path ":" _joined)
    set(ENV{${_runtime_var}} "${_joined}:$ENV{${_runtime_var}}")
endif()
run_step("consumer run" "${_consumer}")
message(STATUS "ProfilesPackageTest: ok")
