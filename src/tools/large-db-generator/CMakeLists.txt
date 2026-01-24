# Large Database Generator Tool
# Enabled by default in Debug, opt-in in Release

option(LIGHTWEIGHT_BUILD_LARGEDB_TOOL "Build large database generator tool" OFF)

# Auto-enable in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LIGHTWEIGHT_BUILD_LARGEDB_TOOL ON)
endif()

if(NOT LIGHTWEIGHT_BUILD_LARGEDB_TOOL)
    message(STATUS "Skipping large-db-generator (set LIGHTWEIGHT_BUILD_LARGEDB_TOOL=ON to build)")
    return()
endif()

message(STATUS "Building large-db-generator tool")

add_executable(large-db-generator
    main.cpp
    ../../tests/LargeTestDatabase/DataGenerator.cpp
)

target_include_directories(large-db-generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tests
)

target_link_libraries(large-db-generator PRIVATE
    Lightweight::Lightweight
    Catch2::Catch2
)

install(TARGETS large-db-generator DESTINATION bin OPTIONAL)
