# stdexec is now a mandatory PUBLIC dependency of the Lightweight library (resolved in the root
# CMakeLists.txt via find_package/CPM), so it arrives transitively through Lightweight::Lightweight.
# The async tests build unconditionally.
find_package(Catch2 REQUIRED)

add_executable(LightweightTest)
target_compile_features(LightweightTest PUBLIC cxx_std_23)
target_compile_definitions(LightweightTest PRIVATE BUILD_TESTS)

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

target_link_libraries(LightweightTest PRIVATE ${TEST_LIBRARIES})

if(MSVC)
    target_compile_options(LightweightTest PRIVATE /bigobj)
    # cl-only stdexec mitigations (clang-cl rejects both as unused — see src/Lightweight/CMakeLists.txt).
    if(NOT CLANG_CL)
        # AsynchronousTests.cpp includes stdexec headers directly, which need MSVC's conforming
        # preprocessor (its config macros misfire under the legacy preprocessor). The other test TUs
        # only see the stdexec-free public headers, so this is harmless for them.
        target_compile_options(LightweightTest PRIVATE /Zc:preprocessor)
        # stdexec headers are SYSTEM (/external:I) includes; lower the external warning level so they
        # do not trip the test target's warnings (mirrors the Lightweight target).
        target_compile_options(LightweightTest PRIVATE /experimental:external /external:W0)
    endif()
endif()

# AsynchronousTests.cpp includes stdexec headers directly; demote the diagnostics stdexec trips
# under -Werror. Guarded off MSVC, where these GCC/Clang flags are not recognized.
set_source_files_properties(DataMapper/AsynchronousTests.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=empty-body>;$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=missing-declarations>;$<$<CXX_COMPILER_ID:Clang>:-Wno-error=newline-eof>;$<$<CXX_COMPILER_ID:Clang>:-Wno-error=macro-redefined>"
)

set(SOURCE_FILES
    ConfigProfileStoreTests.cpp
    CoreTests.cpp
    CxxModelPrinterTests.cpp
    DataBinderTests.cpp
    DataSourceEnumeratorTests.cpp
    PluginDiscoveryTests.cpp
    PluginIngestionTests.cpp
    SecretResolverTests.cpp
    DataMapper/BatchTests.cpp
    DataMapper/CreateTests.cpp
    DataMapper/MiscTests.cpp
    DataMapper/NamingTests.cpp
    DataMapper/ReadTests.cpp
    DataMapper/RelationTests.cpp
    DataMapper/BelongsToStateTests.cpp
    DataMapper/StateTests.cpp
    FieldTests.cpp
    LargeTestDatabase/DataGenerator.cpp
    LargeTestDatabase/LargeTestDatabaseTests.cpp
    MigrationTests.cpp
    MigrationReflectionTests.cpp
    QueryBuilderTests.cpp
    QueryFormatterTests.cpp
    SqlErrorDetectionTests.cpp
    SqlGuidTests.cpp
    SqlLoggerTests.cpp
    MigrationLockTests.cpp
    SqlConnectionDbTests.cpp
    SqlBinaryAndTextDbTests.cpp
    SqlConnectInfoEdgeTests.cpp
    SqlDateTimeArithmeticTests.cpp
    SqlDateTimeDbTests.cpp
    SqlNumericTests.cpp
    SqlRealNameTests.cpp
    SqlSchemaDbTests.cpp
    SqlStatementDbTests.cpp
    SqlTransactionTests.cpp
    ThreadSafeQueueTests.cpp
    SqlVariantAccessorTests.cpp
    SqlVariantDbTests.cpp
    SqlBackup/AllColumnTypesTests.cpp
    SqlBackup/BatchManagerIntegrationTests.cpp
    SqlBackup/CommonHelpersTests.cpp
    SqlBackup/ProgressManagerTests.cpp
    SqlBackup/BatchManagerTests.cpp
    SqlBackup/MsgPackChunkFormatsTests.cpp
    SqlBackup/BatchManagerIntegrationTests.cpp
    SqlBackup/BatchManagerTests.cpp
    SqlBackup/MsgPackChunkFormatsTests.cpp
    SqlBackup/MultiPkTests.cpp
    SqlBackup/Benchmarks.cpp
    SqlBackup/ParseSchemaTests.cpp
    SqlBackup/RobustnessTests.cpp
    SqlBackup/Sha256Tests.cpp
    SqlBackup/TableFilterTests.cpp
    SqlBackup/Tests.cpp
    SqlBackup/SqlBackupCompositeFKTests.cpp
    SqlBackup/SqlBackupIndexTests.cpp
    SqlBackup/ProductionReadinessTests.cpp
    dbtool/StandardProgressManagerTests.cpp
    UnicodeConverterTests.cpp
    Utils.cpp
    UtilsTests.cpp
    Zip/ZipTests.cpp
)

list(APPEND SOURCE_FILES
    DataMapper/AsynchronousTests.cpp
    Async/TaskTests.cpp
    Async/ExecutorTests.cpp
    Async/OffloadTests.cpp
    Async/AsyncDbTests.cpp
    Async/AsyncPoolTests.cpp
    Async/AsyncTransactionTests.cpp
    Async/StdexecBackingTests.cpp
)

target_sources(LightweightTest PRIVATE ${SOURCE_FILES})

enable_testing()

# Optional extra arguments to pass to test executables
# Usage: cmake -DLIGHTWEIGHT_TEST_ARGS="--test-env=sqlite3" ...
# Or:    ctest --preset clang-debug (after configuring with the above)
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
