# Exit-time regression test for #649: a process that applied migrations must exit cleanly.
add_executable(MigrationExitTest MigrationExit.cpp)
target_link_libraries(MigrationExitTest PRIVATE Lightweight)

add_test(NAME MigrationExitTest
    COMMAND MigrationExitTest "DRIVER=${SQLITE_DRIVER_NAME};Database=test_migration_exit.db"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
