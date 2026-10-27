# SPDX-License-Identifier: Apache-2.0
#
# Script-mode test: the Lightweight::Profiles shared library must export nothing but symbols in
# the Lightweight::Profiles namespace. Run with:
#   cmake -DLIB=<path to libLightweightProfiles.so|.dylib> -P cmake/tests/ProfilesExportsTest.cmake
# Registered as the `ProfilesExportsTest` CTest on UNIX. (Windows exports nothing without
# __declspec, so no check is needed there.)

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED LIB OR NOT EXISTS "${LIB}")
    message(FATAL_ERROR "ProfilesExportsTest: pass -DLIB=<shared library>; '${LIB}' does not exist")
endif()

if(APPLE)
    find_program(NM_EXE nm REQUIRED)
    find_program(CXXFILT_EXE c++filt REQUIRED)
    execute_process(COMMAND "${NM_EXE}" -gU "${LIB}" OUTPUT_VARIABLE _raw RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "ProfilesExportsTest: nm failed: ${_err}")
    endif()
    execute_process(COMMAND "${CXXFILT_EXE}" INPUT_VARIABLE _raw OUTPUT_VARIABLE _symbols RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "ProfilesExportsTest: c++filt failed")
    endif()
else()
    find_program(NM_EXE nm REQUIRED)
    execute_process(COMMAND "${NM_EXE}" -D --defined-only -C "${LIB}"
        OUTPUT_VARIABLE _symbols RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "ProfilesExportsTest: nm failed: ${_err}")
    endif()
endif()

string(REGEX REPLACE "\n+$" "" _symbols "${_symbols}")
string(REPLACE "\n" ";" _lines "${_symbols}")

set(_exported 0)
set(_offenders "")
foreach(_line IN LISTS _lines)
    if(_line STREQUAL "")
        continue()
    endif()
    # Linker-provided bookkeeping symbols, not part of the library's API.
    if(_line MATCHES " (_init|_fini|__bss_start|_edata|_end|_*_bss_start|_*_edata|_*_end)$")
        continue()
    endif()
    if(_line MATCHES "Lightweight::Profiles::")
        math(EXPR _exported "${_exported} + 1")
    else()
        list(APPEND _offenders "${_line}")
    endif()
endforeach()

if(_offenders)
    list(JOIN _offenders "\n  " _text)
    message(FATAL_ERROR "ProfilesExportsTest: ${LIB} exports symbols outside Lightweight::Profiles:\n  ${_text}")
endif()
if(_exported EQUAL 0)
    message(FATAL_ERROR "ProfilesExportsTest: ${LIB} exports no Lightweight::Profiles symbols at all")
endif()
message(STATUS "ProfilesExportsTest: ${_exported} exported symbols, all in Lightweight::Profiles")
