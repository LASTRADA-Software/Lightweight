add_library(dummy_migration_plugin MODULE
    Plugin.cpp
)

target_link_libraries(dummy_migration_plugin PRIVATE Lightweight)

# Ensure the library is put in a known location for testing
set_target_properties(dummy_migration_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
)

install(TARGETS dummy_migration_plugin DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/tests/plugins)
