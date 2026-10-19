# Seeds rows with `Insert(...).IfNotExists(...)`. Named without the `Plugin` suffix so
# discovery ignores it in the shared plugins directory; test_dbtool.py opts in by copying.
add_library(seed_row_fixture MODULE
    Plugin.cpp
)

target_link_libraries(seed_row_fixture PRIVATE Lightweight)

set_target_properties(seed_row_fixture PROPERTIES
    OUTPUT_NAME "SeedRowFixture"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    PREFIX ""
)

install(TARGETS seed_row_fixture DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/tests/plugins)
