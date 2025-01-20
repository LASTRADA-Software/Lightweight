#! /usr/bin/env bash

set -e

LS_EXE="$(which ls)"
PROJECT_ROOT="$(realpath $(dirname "$0")/../..)"
BUILD_DIR="${1:-${PROJECT_ROOT}/out/build/linux-clang-debug}"
DDL2CPP="${DDL2CPP:-${BUILD_DIR}/src/tools/ddl2cpp}"

SQLITE_TESTDB_FILE="${PROJECT_ROOT}/test.db"
ODBC_CONNECTION_STRING="DRIVER=SQLite3;Database=${SQLITE_TESTDB_FILE}"

cleanup()
{
    rm -f "${SQLITE_TESTDB_FILE}"
}

# run cleanup on script exit
trap cleanup EXIT

run_test() {
    echo "Running test $1"

    local actual_result_file="${PROJECT_ROOT}/src/tools/tests/$1.result"
    local expect_result_file="${PROJECT_ROOT}/src/tools/tests/$1.expected"

    sqlite3 "${SQLITE_TESTDB_FILE}" < ./src/tools/tests/$1.sql
    ${DDL2CPP} --connection-string "${ODBC_CONNECTION_STRING}" --output "${actual_result_file}" 1> /dev/null
    diff -u ./src/tools/tests/$1.result ${expect_result_file} --ignore-all-space --ignore-blank-lines || true

    rm -f "${SQLITE_TESTDB_FILE}"
    rm -f "${actual_result_file}"
}

for test in $("${LS_EXE}" -1 ${PROJECT_ROOT}/src/tools/tests/*.sql); do
    run_test $(basename $test .sql)
done
