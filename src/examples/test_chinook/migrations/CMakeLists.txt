add_library(chinook_migrations MODULE
    Plugin.cpp
)

target_link_libraries(chinook_migrations PRIVATE Lightweight)
target_compile_features(chinook_migrations PUBLIC cxx_std_23)

set_target_properties(chinook_migrations PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/plugins"
)

install(TARGETS chinook_migrations DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/examples/plugins)
