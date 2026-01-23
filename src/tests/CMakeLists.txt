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

set(TEST_LIBRARIES Catch2::Catch2 Lightweight::Lightweight Lightweight::Tools yaml-cpp::yaml-cpp dbtool_lib zip nlohmann_json::nlohmann_json)
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
add_test(NAME LightweightTest COMMAND LightweightTest)

install(TARGETS LightweightTest DESTINATION bin)
install(FILES test_dbtool.py DESTINATION ${CMAKE_INSTALL_DATADIR}/Lightweight/tests)

add_subdirectory(dummy_migration_plugin)
add_subdirectory(dummy_migration_plugin_2)

# Register dbtool integration test
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME DbToolIntegrationTest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_dbtool.py
        --dbtool $<TARGET_FILE:dbtool>
        --plugins-dir ${CMAKE_BINARY_DIR}/tests/plugins
        --connection-string "DRIVER=SQLite3;Database=test_dbtool_ctest.db"
)
