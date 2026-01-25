# SPDX-License-Identifier: Apache-2.0

# Static library for testing
add_library(lup2dbtool_lib STATIC
    LupVersionConverter.cpp
    SqlStatementParser.cpp
    LupSqlParser.cpp
    CodeGenerator.cpp
)
target_include_directories(lup2dbtool_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lup2dbtool_lib PUBLIC Lightweight::Lightweight)

# Enable coverage instrumentation
enable_coverage_for_target(lup2dbtool_lib)

# Executable
add_executable(lup2dbtool main.cpp)
target_link_libraries(lup2dbtool PRIVATE lup2dbtool_lib)
enable_coverage_for_target(lup2dbtool)
install(TARGETS lup2dbtool DESTINATION bin)
