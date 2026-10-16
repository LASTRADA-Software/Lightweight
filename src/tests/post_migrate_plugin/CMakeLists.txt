add_library(post_migrate_plugin MODULE
    Plugin.cpp
)

target_link_libraries(post_migrate_plugin PRIVATE Lightweight)

# Lives next to the dummy plugins so test_dbtool.py picks it up with the same
# --plugins-dir. OUTPUT_NAME carries the `*Plugin` suffix discovery requires.
set_target_properties(post_migrate_plugin PROPERTIES
    OUTPUT_NAME "PostMigrateRecorderPlugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    PREFIX ""
)

install(TARGETS post_migrate_plugin DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/tests/plugins)

# A migration that always fails. Named without the `Plugin` suffix so discovery
# ignores it in the shared plugins directory; test_dbtool.py opts in by copying.
add_library(failing_migration_fixture MODULE
    FailingMigration.cpp
)

target_link_libraries(failing_migration_fixture PRIVATE Lightweight)

set_target_properties(failing_migration_fixture PROPERTIES
    OUTPUT_NAME "FailingMigrationFixture"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    PREFIX ""
)

install(TARGETS failing_migration_fixture DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/tests/plugins)
