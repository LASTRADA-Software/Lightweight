add_library(dummy_migration_plugin_2 MODULE
    Plugin.cpp
)

target_link_libraries(dummy_migration_plugin_2 PRIVATE Lightweight)

# Ensure the library is put in a known location for testing.
# OUTPUT_NAME aligns the built filename with the `*Plugin` suffix that
# `Lightweight::Tools::DiscoverPlugins` requires — without this the file
# would be called `dummy_migration_plugin_2.{dll,so,dylib}` and discovery
# would silently skip it.
set_target_properties(dummy_migration_plugin_2 PROPERTIES
    OUTPUT_NAME "DummyMigration2Plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    PREFIX ""
)

install(TARGETS dummy_migration_plugin_2 DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/tests/plugins)
