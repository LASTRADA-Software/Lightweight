# Installs a compat policy from a static initializer, like an application's migration plugin.
# Named without the `Plugin` suffix so discovery ignores it in the shared plugins directory;
# test_dbtool.py opts in by copying.
add_library(compat_policy_fixture MODULE
    Plugin.cpp
)

target_link_libraries(compat_policy_fixture PRIVATE Lightweight)

set_target_properties(compat_policy_fixture PROPERTIES
    OUTPUT_NAME "CompatPolicyFixture"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins"
    PREFIX ""
)

install(TARGETS compat_policy_fixture DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/tests/plugins)
