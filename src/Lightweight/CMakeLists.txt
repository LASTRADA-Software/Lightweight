if(NOT WIN32)
    find_package(SQLite3 REQUIRED)
    include_directories(${SQLITE3_INCLUDE_DIR})

    # find unixODBC via pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ODBC REQUIRED odbc)
endif()

if(WIN32)
    find_package(libzip CONFIG REQUIRED)
    # vcpkg provides libzip::zip, create alias to match CPM target name
    add_library(zip ALIAS libzip::zip)
else()
    CPMAddPackage(
        NAME libzip
        GITHUB_REPOSITORY nih-at/libzip
        GIT_TAG v1.11.3
        OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_DOC OFF" "BUILD_EXAMPLES OFF" "BUILD_REGRESS OFF" "BUILD_TOOLS OFF" "ENABLE_OPENSSL OFF" "ENABLE_COMMONCRYPTO OFF" "ENABLE_GNUTLS OFF" "ENABLE_MBEDTLS OFF" "ENABLE_WINDOWS_CRYPTO OFF" "ENABLE_ZSTD OFF" "ENABLE_LZMA OFF"
    )
endif()

CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    VERSION 3.11.3
)

set(HEADER_FILES
    DataBinder/BasicStringBinder.hpp
    DataBinder/Core.hpp
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

    DataMapper/BelongsTo.hpp
    DataMapper/DataMapper.hpp
    DataMapper/Error.hpp
    DataMapper/Field.hpp
    DataMapper/HasMany.hpp
    DataMapper/HasManyThrough.hpp
    DataMapper/HasOneThrough.hpp
    DataMapper/QueryBuilders.hpp

    SqlBackup.hpp
    SqlBackup/TableFilter.hpp
    SqlBackupFormats.hpp
    SqlConnectInfo.hpp
    SqlConnection.hpp
    SqlError.hpp
    SqlErrorDetection.hpp
    SqlLogger.hpp
    SqlMigration.hpp
    SqlMigrationLock.hpp
    SqlQueryFormatter.hpp
    SqlSchema.hpp
    SqlScopedTraceLogger.hpp
    SqlServerType.hpp
    SqlStatement.hpp
)

set(SOURCE_FILES
    DataBinder/Primitives.cpp
    DataBinder/SqlGuid.cpp
    DataBinder/SqlVariant.cpp
    DataBinder/UnicodeConverter.cpp

    DataMapper/DataMapper.cpp

    SqlBackup/BatchManager.cpp
    SqlBackup/MsgPackChunkFormats.cpp
    SqlBackup/SqlBackup.cpp
    SqlBackup/TableFilter.cpp
    SqlConnectInfo.cpp
    SqlConnection.cpp
    SqlError.cpp
    SqlLogger.cpp
    SqlMigration.cpp
    SqlMigrationLock.cpp
    SqlQuery.cpp
    SqlQuery/Core.cpp
    SqlQuery/Migrate.cpp
    SqlQuery/MigrationPlan.cpp
    SqlQuery/Select.cpp
    SqlQueryFormatter.cpp
    SqlSchema.cpp
    SqlStatement.cpp
    SqlTransaction.cpp
    Utils.cpp
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

if(LIGHTWEIGHT_BUILD_SHARED)
    target_compile_definitions(Lightweight PRIVATE BUILD_LIGHTWEIGHT=1)
    target_compile_definitions(Lightweight PUBLIC LIGHTWEIGHT_SHARED=1)
    set_target_properties(Lightweight PROPERTIES CXX_VISIBILITY_PRESET hidden)
endif()

add_library(LightweightTools STATIC)
add_library(Lightweight::Tools ALIAS LightweightTools)
target_compile_features(LightweightTools PUBLIC cxx_std_23)
target_sources(LightweightTools PRIVATE
    Tools/CxxModelPrinter.cpp
    Tools/CxxModelPrinter.hpp
)
target_link_libraries(LightweightTools PUBLIC Lightweight)

if(CLANG_TIDY_EXE)
    set_target_properties(Lightweight PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    set_target_properties(LightweightTools PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

target_include_directories(Lightweight PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(Lightweight PUBLIC reflection-cpp::reflection-cpp)
target_link_libraries(Lightweight PRIVATE $<BUILD_INTERFACE:nlohmann_json::nlohmann_json> zip)

if(MSVC)
    target_compile_options(Lightweight PRIVATE /W4 /WX)
    # clang-cl does not support /MP
    if(NOT CLANG_CL)
        target_compile_options(Lightweight PRIVATE /MP)
    endif()
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

    # Link libzip
    if(LIBZIP_FOUND)
        # System libzip via pkg-config
        target_include_directories(Lightweight PRIVATE ${LIBZIP_INCLUDE_DIRS})
        target_link_libraries(Lightweight PUBLIC ${LIBZIP_LDFLAGS})
    else()
        # CPM-fetched libzip
        target_link_libraries(Lightweight PUBLIC libzip::zip)
    endif()
endif()

set_target_properties(Lightweight PROPERTIES
    EXPORT_NAME Lightweight
    OUTPUT_NAME Lightweight
    COMPILE_PDB_NAME Lightweight
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" # The directory where the PDB file is generated
)
if(NOT WIN32)
    set_target_properties(Lightweight PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# We want to set version and soversion not on windows since linker does not like the version that we are using
if(NOT WIN32)
    set_target_properties(Lightweight PROPERTIES
        VERSION "${CMAKE_PROJECT_VERSION}"
        SOVERSION "${CMAKE_PROJECT_VERSION_MAJOR}"
    )
endif()

# ==================================================================================================

include(GNUInstallDirs) # Provides standard installation directory variables like CMAKE_INSTALL_LIBDIR

# install header files recursively
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Lightweight
    FILES_MATCHING PATTERN "*.hpp"
)

install(TARGETS Lightweight
    EXPORT Lightweight-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} # For DLLs on Windows
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} # For shared libs (.so, .dll) and Windows import libs (.lib)
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} # For static libs (.a, .lib)
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} # IMPORTANT: Needed for interface libraries / header locations
)

# Only install the PDB file for Debug and RelWithDebInfo configurations on Windows platform
if(WIN32)
    install(FILES $<TARGET_PDB_FILE:Lightweight>
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        CONFIGURATIONS Debug RelWithDebInfo
        COMPONENT DebugSymbols
        OPTIONAL
    )
endif()

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/Lightweight-config-version.cmake"
    VERSION ${CMAKE_PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/Lightweight-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Lightweight-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Lightweight" # Standard location
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR # Variables to make absolute in config file
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/Lightweight-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/Lightweight-config-version.cmake"
    DESTINATION
        "${CMAKE_INSTALL_LIBDIR}/cmake/Lightweight" # Same dir as INSTALL_DESTINATION above
)

install(
    EXPORT Lightweight-targets # Must match the EXPORT name used in install(TARGETS)
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Lightweight" # Same dir as config files
    NAMESPACE Lightweight:: # The namespace for imported targets (e.g., Lightweight::Lightweight)
)

include(CPack)
