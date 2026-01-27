add_executable(ddl2cpp ddl2cpp.cpp)
target_link_libraries(ddl2cpp PRIVATE Lightweight::Lightweight Lightweight::Tools yaml-cpp)
install(TARGETS ddl2cpp DESTINATION bin)

add_subdirectory(dbtool)
add_subdirectory(large-db-generator)
add_subdirectory(lup2dbtool)
add_subdirectory(LupMigrationsPlugin)
