add_library(dbtool_lib STATIC
    PluginLoader.cpp
    PluginLoader.hpp
    StandardProgressManager.cpp
    StandardProgressManager.hpp
)

target_link_libraries(dbtool_lib PUBLIC Lightweight yaml-cpp)

add_executable(dbtool main.cpp)
if(WIN32)
    find_package(libzip CONFIG REQUIRED)
    target_link_libraries(dbtool PRIVATE dbtool_lib libzip::zip)
else()
    target_link_libraries(dbtool PRIVATE dbtool_lib zip)
endif()

if(UNIX)
    target_link_libraries(dbtool_lib PRIVATE ${CMAKE_DL_LIBS})
endif()

install(TARGETS dbtool DESTINATION bin)

# Enable coverage instrumentation
enable_coverage_for_target(dbtool_lib)
enable_coverage_for_target(dbtool)
