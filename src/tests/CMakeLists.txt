option(LIGHTWEIGHT_BUILD_TESTS "Build Lightweight tests" ON)

if(NOT LIGHTWEIGHT_BUILD_TESTS)
    return()
endif()

# Try to find TBB for stdexec support
find_package(TBB QUIET)

if(TBB_FOUND)
    message(STATUS "TBB found - building with stdexec support")
    set(LIGHTWEIGHT_HAS_STDEXEC ON)

    set(STDEXEC_BUILD_TESTS OFF)
    set(STDEXEC_BUILD_EXAMPLES OFF)
    set(STDEXEC_ENABLE_TBB ON)

    CPMAddPackage(
      NAME stdexec
      GITHUB_REPOSITORY NVIDIA/stdexec
      GIT_TAG nvhpc-25.09
    )
else()
    message(STATUS "TBB not found - building without stdexec support (AsynchronousTests will be skipped)")
    set(LIGHTWEIGHT_HAS_STDEXEC OFF)
endif()

find_package(Catch2 REQUIRED)

add_executable(LightweightTest)
target_compile_features(LightweightTest PUBLIC cxx_std_23)

if(WIN32)
    find_package(libzip CONFIG REQUIRED)
    set(ZIP_LIB libzip::zip)
else()
    set(ZIP_LIB zip)
endif()
set(TEST_LIBRARIES Catch2::Catch2 Lightweight::Lightweight Lightweight::Tools yaml-cpp::yaml-cpp dbtool_lib ${ZIP_LIB} nlohmann_json::nlohmann_json)
if(MSVC)
    # clang-cl does not support /MP
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(Lightweight PRIVATE /MP)
    endif()
else()
    set(TEST_LIBRARIES ${TEST_LIBRARIES} odbc ${SQLITE3_LIBRARY}) # FIXME: should be PkgConfig::ODBC in Lightweight target already
endif()

if(LIGHTWEIGHT_HAS_STDEXEC)
    target_link_libraries(LightweightTest PRIVATE ${TEST_LIBRARIES} STDEXEC::stdexec)
else()
    target_link_libraries(LightweightTest PRIVATE ${TEST_LIBRARIES})
endif()

if(MSVC)
    target_compile_options(LightweightTest PRIVATE /bigobj)
endif()

# some flags required to compile stdexec with pedantic settings
if(LIGHTWEIGHT_HAS_STDEXEC)
    target_compile_options(LightweightTest PRIVATE -Wno-error=empty-body -Wno-error=missing-declarations)
endif()

set(SOURCE_FILES
    CoreTests.cpp
    CxxModelPrinterTests.cpp
    DataBinderTests.cpp
    DataMapper/CreateTests.cpp
    DataMapper/MiscTests.cpp
    DataMapper/NamingTests.cpp
    DataMapper/ReadTests.cpp
    DataMapper/RelationTests.cpp
    LargeTestDatabase/DataGenerator.cpp
    LargeTestDatabase/LargeTestDatabaseTests.cpp
    MigrationTests.cpp
    MigrationReflectionTests.cpp
    QueryBuilderTests.cpp
    SqlBackup/BatchManagerIntegrationTests.cpp
    SqlBackup/BatchManagerTests.cpp
    SqlBackup/MsgPackChunkFormatsTests.cpp
    SqlBackup/BatchManagerIntegrationTests.cpp
    SqlBackup/BatchManagerTests.cpp
    SqlBackup/MsgPackChunkFormatsTests.cpp
    SqlBackup/MultiPkTests.cpp
    SqlBackup/Benchmarks.cpp
    SqlBackup/RobustnessTests.cpp
    SqlBackup/TableFilterTests.cpp
    SqlBackup/Tests.cpp
    SqlBackup/SqlBackupCompositeFKTests.cpp
    SqlBackup/SqlBackupIndexTests.cpp
    SqlBackup/ProductionReadinessTests.cpp
    dbtool/StandardProgressManagerTests.cpp
    UnicodeConverterTests.cpp
    Utils.cpp
    UtilsTests.cpp
)

if(LIGHTWEIGHT_HAS_STDEXEC)
    list(APPEND SOURCE_FILES DataMapper/AsynchronousTests.cpp)
endif()

target_sources(LightweightTest PRIVATE ${SOURCE_FILES})

enable_testing()

# Optional extra arguments to pass to test executables
# Usage: cmake -DLIGHTWEIGHT_TEST_ARGS="--test-env=sqlite3" ...
# Or:    ctest --preset linux-clang-debug (after configuring with the above)
set(LIGHTWEIGHT_TEST_ARGS "" CACHE STRING "Extra arguments to pass to test executables")
separate_arguments(LIGHTWEIGHT_TEST_ARGS_LIST NATIVE_COMMAND "${LIGHTWEIGHT_TEST_ARGS}")

add_test(NAME LightweightTest COMMAND LightweightTest ${LIGHTWEIGHT_TEST_ARGS_LIST})

install(TARGETS LightweightTest DESTINATION bin)
install(FILES test_dbtool.py DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/tests)

add_subdirectory(dummy_migration_plugin)
add_subdirectory(dummy_migration_plugin_2)

# Enable coverage instrumentation and add coverage targets
if(ENABLE_COVERAGE)
    enable_coverage_for_target(LightweightTest)
    add_coverage_targets(LightweightTest)
endif()

# Register dbtool integration test
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(WIN32)
    set(SQLITE_DRIVER_NAME "{SQLite3 ODBC Driver}")
else()
    set(SQLITE_DRIVER_NAME "SQLite3")
endif()
add_test(NAME DbToolIntegrationTest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_dbtool.py
        --dbtool $<TARGET_FILE:dbtool>
        --plugins-dir $<TARGET_FILE_DIR:dummy_migration_plugin>
        --connection-string "DRIVER=${SQLITE_DRIVER_NAME};Database=test_dbtool_ctest.db"
        ${LIGHTWEIGHT_TEST_ARGS_LIST}
)
