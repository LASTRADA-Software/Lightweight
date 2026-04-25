if(LIGHTWEIGHT_BUILD_SHARED)
    set(LIGHTWEIGHT_LIBRARY_TYPE SHARED)
else()
    set(LIGHTWEIGHT_LIBRARY_TYPE STATIC)
endif()

# DataMapper pool configuration
set(LIGHTWEIGHT_POOL_INITIAL_SIZE "4" CACHE STRING "Initial number of pre-created DataMappers in the global pool")
set(LIGHTWEIGHT_POOL_MAX_SIZE "16" CACHE STRING "Maximum pool size (used by BoundedWait and BoundedOverflow)")
set(LIGHTWEIGHT_POOL_GROWTH_STRATEGY "BoundedOverflow" CACHE STRING "Pool growth strategy: BoundedWait, BoundedOverflow, or UnboundedGrow")

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
endif()
# Note: libzip for non-Windows is handled in root CMakeLists.txt (system pkg-config or CPM fallback)

if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found, downloading via CPM")
  CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    VERSION 3.11.3
    EXCLUDE_FROM_ALL YES
  )
endif()

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

    CodeGen/SplitFileWriter.hpp

    DataMapper/BelongsTo.hpp
    DataMapper/DataMapper.hpp
    DataMapper/Error.hpp
    DataMapper/Pool.hpp
    DataMapper/Field.hpp
    DataMapper/HasMany.hpp
    DataMapper/HasManyThrough.hpp
    DataMapper/HasOneThrough.hpp
    DataMapper/QueryBuilders.hpp

    SqlBackup.hpp
    SqlBackup/Backup.hpp
    SqlBackup/Common.hpp
    SqlBackup/Restore.hpp
    SqlBackup/TableFilter.hpp
    SqlBackupFormats.hpp

    Zip/ZipArchive.hpp
    Zip/ZipEntry.hpp
    Zip/ZipError.hpp

    Config/ProfileStore.hpp

    Odbc/DataSourceEnumerator.hpp

    Secrets/ISecretBackend.hpp
    Secrets/SecretResolver.hpp
    Secrets/backends/EnvBackend.hpp
    Secrets/backends/FileBackend.hpp
    Secrets/backends/StdinBackend.hpp

    SqlConnectInfo.hpp
    SqlConnection.hpp
    SqlError.hpp
    SqlErrorDetection.hpp
    SqlLogger.hpp
    SqlMigration.hpp
    SqlMigrationLock.hpp
    SqlOdbcWide.hpp
    SqlQueryFormatter.hpp
    SqlDataDiff.hpp
    SqlSchema.hpp
    SqlSchemaDiff.hpp
    SqlScopedTraceLogger.hpp
    SqlServerType.hpp
    SqlStatement.hpp
    TracyProfiler.hpp

    tui/MarkdownInline.hpp
    tui/MarkdownTable.hpp
    tui/SgrBuilder.hpp
    tui/Style.hpp
    tui/Unicode.hpp
)

set(SOURCE_FILES
    DataBinder/Primitives.cpp
    DataBinder/SqlGuid.cpp
    DataBinder/SqlVariant.cpp
    DataBinder/UnicodeConverter.cpp

    DataMapper/DataMapper.cpp
    DataMapper/Pool.cpp

    SqlBackup/Backup.cpp
    SqlBackup/BatchManager.cpp
    SqlBackup/Common.cpp
    SqlBackup/MsgPackChunkFormats.cpp
    SqlBackup/Restore.cpp
    SqlBackup/SqlBackup.cpp
    SqlBackup/TableFilter.cpp

    Config/ProfileStore.cpp

    Odbc/DataSourceEnumerator.cpp

    Secrets/SecretResolver.cpp
    Secrets/backends/EnvBackend.cpp
    Secrets/backends/FileBackend.cpp
    Secrets/backends/StdinBackend.cpp

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

    CodeGen/SplitFileWriter.cpp
    SqlQueryFormatter.cpp
    SqlDataDiff.cpp
    SqlSchema.cpp
    SqlSchemaDiff.cpp
    SqlStatement.cpp
    SqlTransaction.cpp
    Utils.cpp

    Zip/ZipArchive.cpp
    Zip/ZipEntry.cpp
)

add_library(Lightweight ${LIGHTWEIGHT_LIBRARY_TYPE})

add_library(Lightweight::Lightweight ALIAS Lightweight)
target_compile_features(Lightweight PUBLIC cxx_std_23)
target_sources(Lightweight PRIVATE ${SOURCE_FILES})

if(LIGHTWEIGHT_BUILD_MODULES)
    target_sources(Lightweight
        PUBLIC
            FILE_SET CXX_MODULES FILES
                Lightweight.cppm
    )
endif()

if(LIGHTWEIGHT_BUILD_SHARED)
    target_compile_definitions(Lightweight PRIVATE BUILD_LIGHTWEIGHT=1)
    target_compile_definitions(Lightweight PUBLIC LIGHTWEIGHT_SHARED=1)
    set_target_properties(Lightweight PROPERTIES CXX_VISIBILITY_PRESET hidden)
endif()

target_compile_definitions(Lightweight PUBLIC
    LIGHTWEIGHT_POOL_INITIAL_SIZE=${LIGHTWEIGHT_POOL_INITIAL_SIZE}
    LIGHTWEIGHT_POOL_MAX_SIZE=${LIGHTWEIGHT_POOL_MAX_SIZE}
    LIGHTWEIGHT_POOL_GROWTH_STRATEGY=${LIGHTWEIGHT_POOL_GROWTH_STRATEGY}
)

add_library(LightweightTools STATIC)
add_library(Lightweight::Tools ALIAS LightweightTools)
target_compile_features(LightweightTools PUBLIC cxx_std_23)
target_sources(LightweightTools PRIVATE
    Tools/CxxModelPrinter.cpp
    Tools/CxxModelPrinter.hpp
)
target_link_libraries(LightweightTools PUBLIC Lightweight)

# LightweightTui — vendored terminal/markdown rendering helpers (tui/).
# Kept as a separate static library so its symbols don't need to cross the
# Lightweight DLL ABI on Windows. libunicode stays a private detail.
add_library(LightweightTui STATIC
    tui/MarkdownInline.hpp
    tui/MarkdownTable.cpp
    tui/MarkdownTable.hpp
    tui/SgrBuilder.cpp
    tui/SgrBuilder.hpp
    tui/Style.hpp
    tui/Unicode.cpp
    tui/Unicode.hpp
)
add_library(Lightweight::Tui ALIAS LightweightTui)
target_compile_features(LightweightTui PUBLIC cxx_std_23)
target_include_directories(LightweightTui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)
if(NOT WIN32)
    set_target_properties(LightweightTui PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

include("${CMAKE_SOURCE_DIR}/cmake/FindOrFetchLibunicode.cmake")
if(TARGET unicode::unicode)
    target_link_libraries(LightweightTui PRIVATE unicode::unicode)
elseif(TARGET unicode::core)
    target_link_libraries(LightweightTui PRIVATE unicode::core)
endif()

if(CLANG_TIDY_EXE)
    set_target_properties(Lightweight PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    set_target_properties(LightweightTools PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    set_target_properties(LightweightTui PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

# Enable coverage instrumentation for library targets
enable_coverage_for_target(Lightweight)
enable_coverage_for_target(LightweightTools)
enable_coverage_for_target(LightweightTui)

target_include_directories(Lightweight PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(Lightweight PUBLIC reflection-cpp::reflection-cpp)
target_link_libraries(Lightweight PRIVATE $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)

# yaml-cpp is used only by Config/ProfileStore.cpp; linkage is PRIVATE so it
# does not propagate to library consumers. `find_package(yaml-cpp)` runs in
# the top-level CMakeLists.txt.
target_link_libraries(Lightweight PRIVATE yaml-cpp::yaml-cpp)

if(LIGHTWEIGHT_ENABLE_TRACY)
    target_compile_definitions(Lightweight PUBLIC LIGHTWEIGHT_TRACY_ENABLED)
    target_link_libraries(Lightweight PUBLIC Tracy::TracyClient)
endif()

if(MSVC)
    if(PEDANTIC_COMPILER)
        set(_pedantic_flags /W4)
        if(PEDANTIC_COMPILER_WERROR)
            list(APPEND _pedantic_flags /WX)
        endif()
        target_compile_options(Lightweight PRIVATE ${_pedantic_flags})
        unset(_pedantic_flags)
    endif()
    # clang-cl does not support /MP
    if(NOT CLANG_CL)
        target_compile_options(Lightweight PRIVATE /MP)
    endif()
    target_link_libraries(Lightweight PUBLIC odbc32)
    # Link libzip (vcpkg provides libzip::zip, aliased to zip above)
    target_link_libraries(Lightweight PRIVATE zip)
else()
    if(PEDANTIC_COMPILER)
        set(_pedantic_flags -Wall -Wextra -pedantic)
        if(PEDANTIC_COMPILER_WERROR)
            list(APPEND _pedantic_flags -Werror)
        endif()
        target_compile_options(Lightweight PRIVATE ${_pedantic_flags})
        unset(_pedantic_flags)
    endif()
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

    # Link libzip (zip target is created in root CMakeLists.txt)
    target_link_libraries(Lightweight PRIVATE zip)
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
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Lightweight # Install module files if present
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
