add_library(dummy_migration_plugin MODULE
    Plugin.cpp
)

target_link_libraries(dummy_migration_plugin PRIVATE Lightweight)

# Ensure the library is put in a known location for testing.
# OUTPUT_NAME aligns the built filename with the `*Plugin` suffix that
# `Lightweight::Tools::DiscoverPlugins` requires — without this the file
# would be called `dummy_migration_plugin.{dll,so,dylib}` and discovery
# would silently skip it.
set_target_properties(dummy_migration_plugin PROPERTIES
    OUTPUT_NAME "DummyMigrationPlugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    PREFIX ""
)

install(TARGETS dummy_migration_plugin DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/tests/plugins)
