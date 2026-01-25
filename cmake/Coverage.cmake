# Coverage.cmake - Code coverage support for GCC/Clang
#
# Options:
#   ENABLE_COVERAGE - Enable code coverage instrumentation
#
# Functions:
#   enable_coverage_for_target(TARGET) - Enable coverage for a specific target
#
# Targets:
#   coverage        - Run tests and generate HTML coverage report
#   coverage-clean  - Clean coverage data files
#
# Requirements:
#   - lcov (for coverage data collection)
#   - genhtml (for HTML report generation)
#
# Usage:
#   cmake --preset linux-clang-coverage
#   cmake --build --preset linux-clang-coverage
#   cmake --build --preset linux-clang-coverage --target coverage

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
    find_program(LLVM_COV_PATH llvm-cov)
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

# Function to add coverage targets (call this after defining test targets)
function(add_coverage_targets TEST_TARGET)
    if(NOT LCOV_PATH OR NOT GENHTML_PATH)
        message(STATUS "[Coverage] Skipping coverage targets (lcov/genhtml not found)")
        return()
    endif()

    # Create coverage output directory
    file(MAKE_DIRECTORY ${COVERAGE_OUTPUT_DIR})

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

        # Run the tests against all supported databases
        COMMAND ${CMAKE_COMMAND} -E echo "Running tests for coverage - SQLite3..."
        COMMAND $<TARGET_FILE:${TEST_TARGET}> --test-env=sqlite3 || true
        COMMAND ${CMAKE_COMMAND} -E echo "Running tests for coverage - PostgreSQL..."
        COMMAND $<TARGET_FILE:${TEST_TARGET}> --test-env=postgres || true
        COMMAND ${CMAKE_COMMAND} -E echo "Running tests for coverage - MSSQL..."
        COMMAND $<TARGET_FILE:${TEST_TARGET}> --test-env=mssql || true

        # Capture coverage data
        COMMAND ${LCOV_PATH}
            --capture
            --directory ${CMAKE_BINARY_DIR}
            --output-file ${COVERAGE_INFO_FILE}
            --ignore-errors mismatch,inconsistent
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
            --ignore-errors unused,inconsistent
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
            --ignore-errors inconsistent
            --rc branch_coverage=1

        # Print summary
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated at: ${COVERAGE_HTML_DIR}/index.html"
        COMMAND ${LCOV_PATH} --summary ${COVERAGE_INFO_FILE} --ignore-errors inconsistent --rc branch_coverage=1

        DEPENDS ${TEST_TARGET}
        COMMENT "Generating code coverage report"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endfunction()
