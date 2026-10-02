# SPDX-License-Identifier: Apache-2.0
#
# Script-mode tests for cmake/DbtoolKeyRing.cmake. Run with:
#   cmake -P cmake/tests/DbtoolKeyRingTest.cmake
# Registered as the `dbtool-keyring-config` CTest when tests are built.

cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/../DbtoolKeyRing.cmake")

set(_failures 0)
set(_scratch "${CMAKE_CURRENT_BINARY_DIR}/dbtool-keyring-test")
file(MAKE_DIRECTORY "${_scratch}")

string(REPEAT "a" 64 KEY_A)
string(REPEAT "B" 64 KEY_B)
set(DEV_SPEC "dev=ba456ca44382fdc8bfd9c84ac989779fa0da3754722e2691f1e0d7a735a3b232")

macro(expect_result name file env expectedSpec expectedRelease expectedErrorRegex)
    dbtool_resolve_key_ring("${file}" "${env}" _spec _release _error)
    if(NOT "${expectedErrorRegex}" STREQUAL "")
        if(NOT _error MATCHES "${expectedErrorRegex}")
            message(SEND_ERROR "${name}: expected error matching '${expectedErrorRegex}', got '${_error}'")
            math(EXPR _failures "${_failures} + 1")
        elseif(_error MATCHES "${KEY_A}|${KEY_B}")
            message(SEND_ERROR "${name}: error message leaks key material")
            math(EXPR _failures "${_failures} + 1")
        endif()
    else()
        if(NOT "${_error}" STREQUAL "")
            message(SEND_ERROR "${name}: unexpected error '${_error}'")
            math(EXPR _failures "${_failures} + 1")
        elseif(NOT "${_spec}" STREQUAL "${expectedSpec}" OR NOT "${_release}" STREQUAL "${expectedRelease}")
            message(SEND_ERROR "${name}: got spec='${_spec}' release='${_release}'")
            math(EXPR _failures "${_failures} + 1")
        endif()
    endif()
endmacro()

# No key anywhere: public development key.
expect_result("no key" "" "" "${DEV_SPEC}" "false" "")

# Environment variable (GitHub Actions secret exported as env).
expect_result("env single" "" "v1=${KEY_A}" "v1=${KEY_A}" "true" "")
expect_result("env ring" "" "v2=${KEY_B},v1=${KEY_A}" "v2=${KEY_B},v1=${KEY_A}" "true" "")

# Key file (GitLab "File" variable, vcpkg/Conan portfile option): one entry per
# line or comma-separated, surrounding whitespace and CRLF tolerated.
file(WRITE "${_scratch}/ring-lines.txt" "  v2=${KEY_B}\r\nv1=${KEY_A}\r\n\r\n")
expect_result("file lines" "${_scratch}/ring-lines.txt" "" "v2=${KEY_B},v1=${KEY_A}" "true" "")
file(WRITE "${_scratch}/ring-commas.txt" "v1=${KEY_A}\n")
expect_result("file single" "${_scratch}/ring-commas.txt" "" "v1=${KEY_A}" "true" "")

# Errors.
expect_result("file missing" "${_scratch}/does-not-exist.txt" "" "" "" "not found")
file(WRITE "${_scratch}/empty.txt" "\n")
expect_result("file empty" "${_scratch}/empty.txt" "" "" "" "empty")
expect_result("both given" "${_scratch}/ring-commas.txt" "v1=${KEY_A}" "" "" "both")
expect_result("short key" "" "v1=abc" "" "" "malformed")
expect_result("bad id" "" "V1=${KEY_A}" "" "" "malformed")
expect_result("long id" "" "abcdefghijklmnopq=${KEY_A}" "" "" "at most 16")
expect_result("dev id reserved" "" "dev=${KEY_A}" "" "" "reserved")
expect_result("duplicate id" "" "v1=${KEY_A},v1=${KEY_B}" "" "" "duplicate")

# A tree once configured with a CI key ring must not silently fall back to the
# development key when a later (re-)configure runs without the key variables.
macro(expect_downgrade name wasRelease isRelease expectedErrorRegex)
    dbtool_check_key_ring_downgrade("${wasRelease}" "${isRelease}" _downgradeError)
    if("${expectedErrorRegex}" STREQUAL "")
        if(NOT "${_downgradeError}" STREQUAL "")
            message(SEND_ERROR "${name}: unexpected error '${_downgradeError}'")
            math(EXPR _failures "${_failures} + 1")
        endif()
    elseif(NOT _downgradeError MATCHES "${expectedErrorRegex}")
        message(SEND_ERROR "${name}: expected error matching '${expectedErrorRegex}', got '${_downgradeError}'")
        math(EXPR _failures "${_failures} + 1")
    endif()
endmacro()

expect_downgrade("fresh dev tree" "" "false" "")
expect_downgrade("fresh release tree" "" "true" "")
expect_downgrade("release stays release" "TRUE" "true" "")
expect_downgrade("release loses its key" "TRUE" "false" "development key")

file(REMOVE_RECURSE "${_scratch}")

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} DbtoolKeyRing test(s) failed")
endif()
message(STATUS "DbtoolKeyRing: all tests passed")
