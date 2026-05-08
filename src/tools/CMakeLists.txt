add_executable(ddl2cpp ddl2cpp.cpp)
target_link_libraries(ddl2cpp PRIVATE Lightweight::Lightweight Lightweight::Tools yaml-cpp::yaml-cpp)
install(TARGETS ddl2cpp DESTINATION bin)

# tools-private static library (Config / Secrets / SplitFileWriter) shared
# between dbtool, lup2dbtool, and the test suite. Must be declared before
# the tools that link against it.
add_subdirectory(shared)

add_subdirectory(dbtool)
add_subdirectory(large-db-generator)
add_subdirectory(lup2dbtool)
add_subdirectory(LupMigrationsPlugin)
