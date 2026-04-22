add_executable(chinook_migrate
    main.cpp
    ${PROJECT_SOURCE_DIR}/src/tools/dbtool/PluginLoader.cpp
)

target_include_directories(chinook_migrate PRIVATE
    ${PROJECT_SOURCE_DIR}/src/tools/dbtool
)

target_link_libraries(chinook_migrate PRIVATE Lightweight::Lightweight)
target_compile_features(chinook_migrate PUBLIC cxx_std_23)

if(NOT WIN32)
    target_link_libraries(chinook_migrate PRIVATE ${CMAKE_DL_LIBS})
endif()

# Put the executable next to the plugin so the default --plugin resolves.
set_target_properties(chinook_migrate PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/plugins"
)

add_dependencies(chinook_migrate chinook_migrations)
