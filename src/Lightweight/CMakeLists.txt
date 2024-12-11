cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(Lightweight LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

if(NOT WIN32)
    find_package(SQLite3 REQUIRED)
    include_directories(${SQLITE3_INCLUDE_DIR})

    # find unixODBC via pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ODBC REQUIRED odbc)
endif()

set(HEADER_FILES
    DataBinder/BasicStringBinder.hpp
    DataBinder/Core.hpp
    DataBinder/MFCStringLike.hpp
    DataBinder/Primitives.hpp
    DataBinder/SqlDate.hpp
    DataBinder/SqlDateTime.hpp
    DataBinder/SqlFixedString.hpp
    DataBinder/SqlGuid.hpp
    DataBinder/SqlNullValue.hpp
    DataBinder/SqlNumeric.hpp
    DataBinder/SqlText.hpp
    DataBinder/SqlTime.hpp
    DataBinder/SqlVariant.hpp
    DataBinder/StdOptional.hpp
    DataBinder/StdString.hpp
    DataBinder/StdStringView.hpp
    DataBinder/StringLiteral.hpp
    DataBinder/UnicodeConverter.hpp

    SqlQuery.hpp
    SqlQuery/Core.hpp
    SqlQuery/Delete.hpp
    SqlQuery/Insert.hpp
    SqlQuery/Migrate.hpp
    SqlQuery/MigrationPlan.hpp
    SqlQuery/Select.hpp
    SqlQuery/Update.hpp

    DataMapper/DataMapper.hpp
    DataMapper/Detail.hpp
    DataMapper/Field.hpp
    DataMapper/RecordId.hpp

    SqlConnectInfo.hpp
    SqlConnection.hpp
    SqlError.hpp
    SqlLogger.hpp
    SqlMigration.hpp
    SqlQueryFormatter.hpp
    SqlSchema.hpp
    SqlScopedTraceLogger.hpp
    SqlStatement.hpp
)

set(SOURCE_FILES
    DataBinder/SqlGuid.cpp
    DataBinder/SqlVariant.cpp
    DataBinder/UnicodeConverter.cpp

    SqlConnectInfo.cpp
    SqlConnection.cpp
    SqlError.cpp
    SqlLogger.cpp
    SqlMigration.cpp
    SqlQuery.cpp
    SqlQuery/Migrate.cpp
    SqlQuery/MigrationPlan.cpp
    SqlQuery/Select.cpp
    SqlQueryFormatter.cpp
    SqlSchema.cpp
    SqlStatement.cpp
    SqlTransaction.cpp
)

set(LIGHTWEIGHT_BUILD_SHARED_DEFAULT OFF)
if(WIN32)
    set(LIGHTWEIGHT_BUILD_SHARED_DEFAULT ON)
endif()

option(LIGHTWEIGHT_BUILD_SHARED "Build Lightweight as a shared library" ${LIGHTWEIGHT_BUILD_SHARED_DEFAULT})

if(LIGHTWEIGHT_BUILD_SHARED)
    set(LIGHTWEIGHT_LIBRARY_TYPE SHARED)
else()
    set(LIGHTWEIGHT_LIBRARY_TYPE STATIC)
endif()

add_library(Lightweight ${LIGHTWEIGHT_LIBRARY_TYPE})

add_library(Lightweight::Lightweight ALIAS Lightweight)
target_compile_features(Lightweight PUBLIC cxx_std_23)
target_sources(Lightweight PRIVATE ${SOURCE_FILES})
target_sources(Lightweight PUBLIC ${HEADER_FILES})

if(LIGHTWEIGHT_BUILD_SHARED)
    target_compile_definitions(Lightweight PRIVATE BUILD_LIGHTWEIGHT=1)
    target_compile_definitions(Lightweight PUBLIC LIGHTWEIGHT_SHARED=1)
    set_target_properties(Lightweight PROPERTIES CXX_VISIBILITY_PRESET hidden)
endif()

if(CLANG_TIDY_EXE)
    set_target_properties(Lightweight PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

# target_include_directories(Lightweight PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>)
target_include_directories(Lightweight PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/include>
)

target_link_libraries(Lightweight PUBLIC reflection-cpp::reflection-cpp)

if(MSVC)
    target_compile_options(Lightweight PRIVATE /W4 /WX)
    target_compile_options(Lightweight PRIVATE /MP)
    target_link_libraries(Lightweight PUBLIC odbc32)
else()
    target_compile_options(Lightweight PRIVATE -Wall -Wextra -pedantic -Werror)
    target_compile_options(Lightweight PUBLIC ${ODBC_CFLAGS})
    target_link_libraries(Lightweight PUBLIC ${ODBC_LDFLAGS})

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_libraries(Lightweight PUBLIC stdc++exp) # GCC >= 14
    endif()

    # Find library libuuid via pkg-config, and if available, link against it
    pkg_check_modules(UUID uuid)
    if(UUID_FOUND)
        target_link_libraries(Lightweight PUBLIC ${UUID_LDFLAGS})
    endif()
endif()

# ==================================================================================================


if(NOT WIN32)
    include(GNUInstallDirs)
endif()

#install(TARGETS LightweightTest DESTINATION bin)
install(TARGETS Lightweight DESTINATION lib)

# install header files recursively
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/Lightweight
    FILES_MATCHING PATTERN "*.hpp"
)

include(CPack)
