#! /usr/bin/env bash

# This script is used to run the ddl2cpp tool on the SQLite test database
# and compare the output with the expected result files.

LS_EXE="$(which ls)"
PROJECT_ROOT="$(realpath $(dirname "$0")/../..)"
BUILD_DIR="${1:-${PROJECT_ROOT}/out/build/linux-clang-debug}"
DDL2CPP="${DDL2CPP:-${BUILD_DIR}/src/tools/ddl2cpp}"

run_test_chinook() {
    # run ddl2cpp to create files
    ${DDL2CPP} \
        --connection-string "${ODBC_CONNECTION_STRING}" \
        --make-aliases \
        --database LightweightTest \
        --schema dbo \
        --output "${PROJECT_ROOT}/src/examples/test_chinook/entities_compare"

    # check the diff between the generated files and the expected files
    # list of files to check Album.hpp Artist.hpp Customer.hpp Employee.hpp Genre.hpp Invoice.hpp InvoiceLine.hpp MediaType.hpp Playlist.hpp PlaylistTrack.hpp Track.hpp

    # get the list of files in the entities directory
    files=$(find "${PROJECT_ROOT}/src/examples/test_chinook/entities_compare" -type f -name "*.hpp" | xargs -n 1 basename)

    for file in ${files}; do
        # check if the file exists
        if [[ -f "${PROJECT_ROOT}/src/examples/test_chinook/entities_compare/${file}" ]]; then
            # check the diff between the generated file and the expected file
            # if diff is not empty we need to return 1

            clang-format -i "${PROJECT_ROOT}/src/examples/test_chinook/entities_compare/${file}"
            diff -u "${PROJECT_ROOT}/src/examples/test_chinook/entities_compare/${file}" "${PROJECT_ROOT}/src/examples/test_chinook/entities/${file}" --ignore-all-space --ignore-blank-lines
            if [[ $? -ne 0 ]]; then
                echo "Diff found in ${file}"
                exit 1
            else
                echo "No diff found in ${file}"
            fi
        else
            echo "File ${file} not found"
        fi
    done
}

run_test_chinook
