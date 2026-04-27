add_executable(ddl2cpp ddl2cpp.cpp)
target_link_libraries(ddl2cpp PRIVATE Lightweight::Lightweight Lightweight::Tools yaml-cpp::yaml-cpp)
install(TARGETS ddl2cpp DESTINATION bin)

add_subdirectory(dbtool)
add_subdirectory(large-db-generator)
add_subdirectory(lup2dbtool)
add_subdirectory(LupMigrationsPlugin)

# Optional: Qt 6 GUI companion to the dbtool CLI. LIGHTWEIGHT_BUILD_GUI is
# gated (and possibly downgraded to OFF) by the top-level CMakeLists after
# probing for Qt, so we only descend if it survived that gate.
if(LIGHTWEIGHT_BUILD_GUI)
    add_subdirectory(dbtool-gui)
endif()
