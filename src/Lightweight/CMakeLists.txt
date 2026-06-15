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

    SqlAdvisoryLock.hpp
    SqlConnectInfo.hpp
    SqlConnection.hpp
    SqlError.hpp
    SqlErrorDetection.hpp
    SqlLogger.hpp
    SqlMigration.hpp
    SqlOdbcWide.hpp
    SqlQueryFormatter.hpp
    SqlSchema.hpp
    SqlScopedLock.hpp
    SqlScopedTraceLogger.hpp
    SqlServerType.hpp
    SqlStatement.hpp
    TracyProfiler.hpp
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

    SqlConnectInfo.cpp
    SqlConnection.cpp
    SqlError.cpp
    SqlLogger.cpp
    SqlMigration.cpp
    SqlQuery.cpp
    SqlQuery/Core.cpp
    SqlQuery/Migrate.cpp
    SqlQuery/MigrationPlan.cpp
    SqlQuery/Select.cpp

    SqlQueryFormatter.cpp
    SqlScopedLock.cpp
    SqlSchema.cpp
    SqlStatement.cpp
    SqlTransaction.cpp
    Utils.cpp

    Zip/ZipArchive.cpp
    Zip/ZipEntry.cpp
)

# C++23 coroutine async API (Task, executors, async ODBC methods) — a first-class part of the library.
list(APPEND HEADER_FILES
    Async/Task.hpp
    Async/SyncWait.hpp
    Async/CancellationToken.hpp
    Async/Executor.hpp
    Async/detail/SerialWorkQueue.hpp
    Async/ThreadPoolExecutor.hpp
    Async/StrandExecutor.hpp
    Async/ManualExecutor.hpp
    Async/Async.hpp
    Async/Backend.hpp
    Async/ThreadOffloadBackend.hpp
    Async/Fwd.hpp
    Async/AsyncSqlTransaction.hpp
    Async/DataMapperAsync.hpp
)
list(APPEND SOURCE_FILES
    Async/ThreadPoolExecutor.cpp
    Async/StrandExecutor.cpp
    Async/ManualExecutor.cpp
    Async/AsyncSqlTransaction.cpp
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

if(CLANG_TIDY_EXE)
    set_target_properties(Lightweight PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    set_target_properties(LightweightTools PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

# Enable coverage instrumentation for library targets
enable_coverage_for_target(Lightweight)
enable_coverage_for_target(LightweightTools)

target_include_directories(Lightweight PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(Lightweight PUBLIC reflection-cpp::reflection-cpp)
target_link_libraries(Lightweight PRIVATE $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>)

# stdexec (C++26 executors) backs the async layer's thread pool (see Async/ThreadPoolExecutor.cpp,
# which is the only TU that includes the stdexec headers — the public headers stay stdexec-free via
# a pimpl). Linked PUBLIC so downstream code can write std::execution senders that interop with
# Lightweight's Async::Task without separately locating stdexec.
target_link_libraries(Lightweight PUBLIC STDEXEC::stdexec)

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
    # The following two mitigations apply to the real `cl` driver only. clang-cl has a conforming
    # preprocessor by default and routes SYSTEM includes through /imsvc (which already silences
    # stdexec's header warnings), so it rejects both flags as unused under -Werror.
    if(NOT CLANG_CL)
        # stdexec requires MSVC's conforming preprocessor; without /Zc:preprocessor its config macros
        # misfire (e.g. __VA_OPT__ handling), so sender adaptors like stdexec::starts_on fail to
        # declare. stdexec's own interface advertises this flag, but it does not reliably propagate
        # through the SYSTEM-include normalization, so set it explicitly on every TU of this target.
        target_compile_options(Lightweight PRIVATE /Zc:preprocessor)
        # stdexec ships as SYSTEM (/external:I) includes, but `cl` only silences external-header
        # warnings when the external warning level is lowered — /external:I alone is a no-op under
        # /WX. Lower it for this target only, so the project's own /W4 /WX still covers our headers.
        if(PEDANTIC_COMPILER AND PEDANTIC_COMPILER_WERROR)
            target_compile_options(Lightweight PRIVATE /experimental:external /external:W0)
        endif()
    endif()
    # When building Lightweight as a DLL on MSVC, classes that legitimately expose
    # STL members (`std::string`, `std::vector`, `std::filesystem::path`, …) or
    # derive from header-only interfaces trigger C4251 / C4275. Both warnings are
    # the standard "STL across the DLL boundary" noise — they don't indicate an
    # ABI bug because the producer and the consumer of the DLL share the same
    # MSVC runtime. Suppress them for both `cl` and `clang-cl`.
    if(LIGHTWEIGHT_BUILD_SHARED)
        target_compile_options(Lightweight PRIVATE /wd4251 /wd4275)
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
        # -isystem (from stdexec's SYSTEM includes) suppresses header-origin warnings, but warnings
        # attributed to deep sender/coroutine template instantiations surface against this target.
        # Demote exactly the diagnostics stdexec is known to trip under -Werror (the set the async
        # tests already proved necessary); scoped narrowly to Lightweight.
        target_compile_options(Lightweight PRIVATE
            -Wno-error=empty-body
            -Wno-error=missing-declarations
            $<$<CXX_COMPILER_ID:Clang>:-Wno-error=newline-eof>
            $<$<CXX_COMPILER_ID:Clang>:-Wno-error=macro-redefined>
        )
    endif()
    target_compile_options(Lightweight PUBLIC ${ODBC_CFLAGS})
    target_link_libraries(Lightweight PUBLIC ${ODBC_LDFLAGS})

    # libstdc++ (GCC >= 14, and Clang when configured with -stdlib=libstdc++) ships the experimental
    # runtime bits for <stacktrace>/std::print in a separate `stdc++exp` library that must be linked
    # explicitly. libc++ (the default for Clang on macOS) does not provide this library, so a blanket
    # "GNU or Clang" link breaks there with `ld: library 'stdc++exp' not found`. Probe for the library
    # instead of guessing from the compiler id, so it is linked exactly when it exists.
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES_SAVED "${CMAKE_REQUIRED_LIBRARIES}")
    set(CMAKE_REQUIRED_LIBRARIES stdc++exp)
    check_cxx_source_compiles("int main() { return 0; }" LIGHTWEIGHT_HAVE_STDCXXEXP)
    set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES_SAVED}")
    unset(CMAKE_REQUIRED_LIBRARIES_SAVED)
    if(LIGHTWEIGHT_HAVE_STDCXXEXP)
        target_link_libraries(Lightweight PUBLIC stdc++exp)
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
    COMPONENT Development
    FILES_MATCHING
        PATTERN "*.hpp"
)

install(TARGETS Lightweight
    EXPORT Lightweight-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}     COMPONENT Runtime      # For DLLs on Windows
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}     COMPONENT Runtime      # For shared libs (.so) and Linux DT_SONAME links
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}     COMPONENT Development  # Static libs / Windows import libs
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Lightweight COMPONENT Development
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
        "${CMAKE_INSTALL_LIBDIR}/cmake/Lightweight"
    COMPONENT Development
)

install(
    EXPORT Lightweight-targets # Must match the EXPORT name used in install(TARGETS)
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Lightweight"
    NAMESPACE Lightweight::
    COMPONENT Development
)

# Note: include(CPack) is invoked from cmake/Packaging.cmake at the top level.
