# Coverage.cmake - Code coverage support for GCC/Clang
#
# Options:
#   ENABLE_COVERAGE - Enable code coverage instrumentation
#
# Functions:
#   enable_coverage_for_target(TARGET) - Enable coverage for a specific target
#
# Targets:
#   coverage        - Run tests against all databases and generate HTML coverage report
#   coverage-<env>  - Run tests against one test environment (see
#                     COVERAGE_TEST_ENVIRONMENTS) and capture a per-environment
#                     lcov tracefile at coverage/<env>.info. CI uploads each
#                     tracefile to Codecov under its own flag; Codecov merges
#                     all flagged uploads of a commit into the combined report.
#   coverage-clean  - Clean coverage data files
#
# Requirements:
#   - lcov (for coverage data collection)
#   - genhtml (for HTML report generation)
#
# Usage:
#   cmake --preset clang-coverage
#   cmake --build --preset clang-coverage
#   cmake --build --preset clang-coverage --target coverage

option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

if(NOT ENABLE_COVERAGE)
    # Provide no-op function when coverage is disabled
    function(enable_coverage_for_target TARGET)
    endfunction()
    return()
endif()

if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU"))
    message(WARNING "[Coverage] Code coverage requires Clang or GCC. Skipping.")
    function(enable_coverage_for_target TARGET)
    endfunction()
    return()
endif()

# Coverage doesn't work well with sanitizers
if(ENABLE_SANITIZER_ADDRESS OR ENABLE_SANITIZER_UNDEFINED OR ENABLE_SANITIZER_THREAD)
    message(WARNING "[Coverage] Sanitizers are enabled. Coverage results may be inaccurate.")
endif()

# Find required tools
find_program(LCOV_PATH lcov)
find_program(GENHTML_PATH genhtml)

if(NOT LCOV_PATH)
    message(WARNING "[Coverage] lcov not found. Coverage targets will not be available.")
    message(WARNING "[Coverage] Install with: sudo apt install lcov (Debian/Ubuntu) or sudo dnf install lcov (Fedora)")
endif()

if(NOT GENHTML_PATH)
    message(WARNING "[Coverage] genhtml not found. Coverage targets will not be available.")
endif()

# For Clang, we need to use llvm-cov gcov instead of gcov
set(GCOV_TOOL_OPTION "")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # apt.llvm.org's installer (llvm.sh) only ever installs version-suffixed binaries
    # (llvm-cov-22, never a bare llvm-cov symlink), so an unversioned lookup silently
    # fails and lcov falls back to the system gcov (from GCC), which cannot parse
    # Clang's gcov-compatible data format ("Incompatible GCC/GCOV version" from
    # geninfo). Try the version matching the active compiler first, then fall back
    # to a few recent majors and finally the unversioned name for other toolchains
    # (e.g. Homebrew LLVM on macOS, which does install a bare llvm-cov).
    if(CMAKE_CXX_COMPILER_VERSION)
        string(REGEX MATCH "^[0-9]+" LLVM_COV_VERSION_SUFFIX "${CMAKE_CXX_COMPILER_VERSION}")
    endif()
    find_program(LLVM_COV_PATH
        NAMES
            "llvm-cov-${LLVM_COV_VERSION_SUFFIX}"
            llvm-cov-22 llvm-cov-21 llvm-cov-20 llvm-cov-19 llvm-cov-18
            llvm-cov
    )
    if(LLVM_COV_PATH)
        # Create a wrapper script for llvm-cov gcov
        set(GCOV_WRAPPER_SCRIPT "${CMAKE_BINARY_DIR}/llvm-gcov-wrapper.sh")
        file(WRITE ${GCOV_WRAPPER_SCRIPT} "#!/bin/sh\nexec ${LLVM_COV_PATH} gcov \"$@\"\n")
        file(CHMOD ${GCOV_WRAPPER_SCRIPT} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
        set(GCOV_TOOL_OPTION --gcov-tool ${GCOV_WRAPPER_SCRIPT})
        message(STATUS "[Coverage] Using llvm-cov gcov for Clang coverage")
    else()
        message(WARNING "[Coverage] llvm-cov not found. Coverage may not work correctly with Clang.")
    endif()
endif()

message(STATUS "[Coverage] Enabling code coverage instrumentation")

# Add global link options for coverage runtime
# This ensures executables linking coverage-instrumented static libraries get the runtime
add_link_options(--coverage)

# Define coverage output directory
set(COVERAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coverage")
set(COVERAGE_INFO_FILE "${COVERAGE_OUTPUT_DIR}/coverage.info")
set(COVERAGE_HTML_DIR "${COVERAGE_OUTPUT_DIR}/html")

# Function to enable coverage for a specific target
# This avoids applying coverage flags to third-party dependencies
function(enable_coverage_for_target TARGET)
    target_compile_options(${TARGET} PRIVATE
        --coverage
        -fno-inline
        -fno-elide-constructors
    )
    target_link_options(${TARGET} PRIVATE --coverage)
    message(STATUS "[Coverage] Enabled for target: ${TARGET}")
endfunction()

# Test environments (see scripts/tests/.test-env.yml) that get a dedicated
# coverage-<env> target each. The default set matches the CI coverage job:
# all three DBMS, with MS SQL exercised through both Microsoft-supported ODBC
# driver generations (Driver 18 against 2022, Driver 17 against 2017).
set(COVERAGE_TEST_ENVIRONMENTS "sqlite3;postgres;mssql2022;mssql2017_odbc17"
    CACHE STRING "Test environments for which per-environment coverage-<env> targets are created")

# Defines target coverage-<env>: zero the counters, run the unit test suite
# and (when the tools are built) the dbtool integration suite against a
# single --test-env, and capture a filtered tracefile at coverage/<env>.info.
# Unlike the combined `coverage` target, the test runs are strict — an
# unreachable database fails the target instead of silently producing a
# tracefile that under-reports coverage.
#
# The per-env tracefiles deliberately carry no branch data (BRDA records):
# they are what CI uploads to Codecov, and Codecov counts a line with any
# untaken branch as "partial" (excluded from the headline percentage), which
# misrepresents C++ line coverage. Branch coverage remains available locally
# through the combined `coverage` HTML target.
function(_add_coverage_env_target TEST_TARGET TEST_ENV)
    set(DBTOOL_COVERAGE_COMMANDS "")
    if(TARGET dbtool AND TARGET dummy_migration_plugin AND Python3_EXECUTABLE)
        set(DBTOOL_COVERAGE_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E echo "Running dbtool tests for coverage - ${TEST_ENV}..."
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/tests/test_dbtool.py
                --dbtool $<TARGET_FILE:dbtool>
                --plugins-dir $<TARGET_FILE_DIR:dummy_migration_plugin>
                --test-env ${TEST_ENV}
        )
        set(DBTOOL_COVERAGE_DEPENDS dbtool dummy_migration_plugin dummy_migration_plugin_2)
    endif()

    add_custom_target(coverage-${TEST_ENV}
        COMMAND ${LCOV_PATH} --zerocounters --directory ${CMAKE_BINARY_DIR} ${GCOV_TOOL_OPTION}

        COMMAND ${CMAKE_COMMAND} -E echo "Running tests for coverage - ${TEST_ENV}..."
        COMMAND $<TARGET_FILE:${TEST_TARGET}> --test-env=${TEST_ENV}

        ${DBTOOL_COVERAGE_COMMANDS}

        # `format` is ignored because Clang emits gcov records at line 0 for
        # coroutine helper functions (__await_suspend_wrapper__*), which
        # lcov >= 2.x otherwise treats as a hard error.
        COMMAND ${LCOV_PATH}
            --capture
            --directory ${CMAKE_BINARY_DIR}
            --output-file ${COVERAGE_OUTPUT_DIR}/${TEST_ENV}.info
            --ignore-errors mismatch,inconsistent,format
            ${GCOV_TOOL_OPTION}

        COMMAND ${LCOV_PATH}
            --remove ${COVERAGE_OUTPUT_DIR}/${TEST_ENV}.info
            "/usr/*"
            "${CMAKE_BINARY_DIR}/_deps/*"
            "${CMAKE_SOURCE_DIR}/src/tests/*"
            "*/catch2/*"
            "*/Catch2/*"
            --output-file ${COVERAGE_OUTPUT_DIR}/${TEST_ENV}.info
            --ignore-errors unused,inconsistent,format
            ${GCOV_TOOL_OPTION}

        COMMAND ${LCOV_PATH} --summary ${COVERAGE_OUTPUT_DIR}/${TEST_ENV}.info
            --ignore-errors inconsistent,format

        DEPENDS ${TEST_TARGET} ${DBTOOL_COVERAGE_DEPENDS}
        COMMENT "Generating ${TEST_ENV} coverage tracefile"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endfunction()

# Function to add coverage targets (call this after defining test targets)
function(add_coverage_targets TEST_TARGET)
    if(NOT LCOV_PATH OR NOT GENHTML_PATH)
        message(STATUS "[Coverage] Skipping coverage targets (lcov/genhtml not found)")
        return()
    endif()

    # Create coverage output directory
    file(MAKE_DIRECTORY ${COVERAGE_OUTPUT_DIR})

    foreach(TEST_ENV IN LISTS COVERAGE_TEST_ENVIRONMENTS)
        _add_coverage_env_target(${TEST_TARGET} ${TEST_ENV})
    endforeach()

    # Target to clean coverage data
    add_custom_target(coverage-clean
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${COVERAGE_OUTPUT_DIR}
        COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" -delete 2>/dev/null || true
        COMMENT "Cleaning coverage data"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    # Target to run tests and generate coverage report
    add_custom_target(coverage
        # Reset coverage counters
        COMMAND ${LCOV_PATH} --zerocounters --directory ${CMAKE_BINARY_DIR} ${GCOV_TOOL_OPTION}

        # Run the tests against all supported databases. gcov counters (.gcda)
        # accumulate across runs, so the single capture below is the union of
        # everything each database exercised. Unreachable databases are
        # tolerated (|| true) for local convenience; CI uses the strict
        # per-environment coverage-<env> targets instead.
        COMMAND ${CMAKE_COMMAND} -E echo "Running tests for coverage - SQLite3..."
        COMMAND $<TARGET_FILE:${TEST_TARGET}> --test-env=sqlite3 || true
        COMMAND ${CMAKE_COMMAND} -E echo "Running tests for coverage - PostgreSQL..."
        COMMAND $<TARGET_FILE:${TEST_TARGET}> --test-env=postgres || true
        COMMAND ${CMAKE_COMMAND} -E echo "Running tests for coverage - MSSQL 2022..."
        COMMAND $<TARGET_FILE:${TEST_TARGET}> --test-env=mssql2022 || true

        # Capture coverage data (`format` ignored for Clang's line-0 coroutine
        # helper records, see _add_coverage_env_target above)
        COMMAND ${LCOV_PATH}
            --capture
            --directory ${CMAKE_BINARY_DIR}
            --output-file ${COVERAGE_INFO_FILE}
            --ignore-errors mismatch,inconsistent,format
            --rc branch_coverage=1
            ${GCOV_TOOL_OPTION}

        # Remove coverage for external/system headers and test files
        COMMAND ${LCOV_PATH}
            --remove ${COVERAGE_INFO_FILE}
            "/usr/*"
            "${CMAKE_BINARY_DIR}/_deps/*"
            "${CMAKE_SOURCE_DIR}/src/tests/*"
            "*/catch2/*"
            "*/Catch2/*"
            --output-file ${COVERAGE_INFO_FILE}
            --ignore-errors unused,inconsistent,format
            --rc branch_coverage=1
            ${GCOV_TOOL_OPTION}

        # Generate HTML report
        COMMAND ${GENHTML_PATH}
            ${COVERAGE_INFO_FILE}
            --output-directory ${COVERAGE_HTML_DIR}
            --title "Lightweight Test Coverage"
            --legend
            --show-details
            --branch-coverage
            --ignore-errors inconsistent,format
            --rc branch_coverage=1

        # Print summary
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated at: ${COVERAGE_HTML_DIR}/index.html"
        COMMAND ${LCOV_PATH} --summary ${COVERAGE_INFO_FILE} --ignore-errors inconsistent,format --rc branch_coverage=1

        DEPENDS ${TEST_TARGET}
        COMMENT "Generating code coverage report"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endfunction()
