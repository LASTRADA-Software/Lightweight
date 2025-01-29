#! /usr/bin/env bash

set -x

LS_EXE="$(which ls)"
PROJECT_ROOT="$(realpath $(dirname "$0")/../..)"
BUILD_DIR="${1:-${PROJECT_ROOT}/out/build/linux-clang-debug}"
DDL2CPP="${DDL2CPP:-${BUILD_DIR}/src/tools/ddl2cpp}"

SQLITE_TESTDB_FILE="${PROJECT_ROOT}/test.db"
ODBC_CONNECTION_STRING="DRIVER=SQLite3;Database=${SQLITE_TESTDB_FILE}"

cleanup()
{
    rm -f "${SQLITE_TESTDB_FILE}" || true
}

# run cleanup on script exit
trap cleanup EXIT

run_test() {
    echo "Running test $1"

    local actual_result_file="${PROJECT_ROOT}/src/tools/tests/$1.result"
    local expect_result_file="${PROJECT_ROOT}/src/tools/tests/$1.expected.cpp"

    # check in the file for any additional commands to the ddl2cpp tool
    # commands starts with --
    # example: -- --use-aliases
    local ddl2cpp_args=$(grep -o -P -- '--\s+.*' ./src/tools/tests/$1.sql | sed 's/--//g')

    sqlite3 "${SQLITE_TESTDB_FILE}" < ./src/tools/tests/$1.sql
    ${DDL2CPP} --connection-string "${ODBC_CONNECTION_STRING}" --output "${actual_result_file}" $ddl2cpp_args  1> /dev/null
    if [[ -f "${expect_result_file}" ]]; then
        diff -u ./src/tools/tests/$1.result ${expect_result_file} --ignore-all-space --ignore-blank-lines || true
    else
        echo "Expected result file not found, copying actual result to expected result"
        cp -p "${actual_result_file}" "${expect_result_file}"
    fi

    rm -f "${SQLITE_TESTDB_FILE}"
    rm -f "${actual_result_file}"
}

for test in $("${LS_EXE}" -1 ${PROJECT_ROOT}/src/tools/tests/*.sql); do
    run_test $(basename $test .sql)
done
