# Fixture plugins for the dbtool-gui workflow tests. Each is named without the `Plugin` suffix, so
# discovery ignores it in the shared plugins directory; the tests opt in by copying it under a
# `*Plugin` name into a scratch plugins directory.
function(lightweight_add_gui_workflow_fixture target outputName source)
    add_library(${target} MODULE ${source})
    target_link_libraries(${target} PRIVATE Lightweight)
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${outputName}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
        PREFIX ""
    )
    install(TARGETS ${target} DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/tests/plugins)
endfunction()

lightweight_add_gui_workflow_fixture(gui_workflow_fixture GuiWorkflowFixture GuiWorkflow.cpp)
lightweight_add_gui_workflow_fixture(migration_requires_backup_fixture MigrationRequiresBackupFixture MigrationRequiresBackup.cpp)
lightweight_add_gui_workflow_fixture(unicode_candidate_fixture UnicodeCandidateFixture UnicodeCandidate.cpp)
lightweight_add_gui_workflow_fixture(bulk_migrations_fixture BulkMigrationsFixture BulkMigrations.cpp)
