# stdexec is now a mandatory PUBLIC dependency of the Lightweight library (resolved in the root
# CMakeLists.txt via find_package/CPM), so it arrives transitively through Lightweight::Lightweight.
# The async tests build unconditionally.
find_package(Catch2 REQUIRED)

add_executable(LightweightTest)
target_compile_features(LightweightTest PUBLIC cxx_std_23)
target_compile_definitions(LightweightTest PRIVATE BUILD_TESTS)

# The `zip` target is resolved once in the root CMakeLists.txt for every platform (find_package
# first, CPM fallback if not found — #543); re-resolving libzip here would hard-fail a build that
# reached the target only via the CPM fallback.
set(TEST_LIBRARIES Catch2::Catch2 Lightweight::Lightweight Lightweight::Tools yaml-cpp::yaml-cpp dbtool_lib Lightweight::Profiles zip nlohmann_json::nlohmann_json)
if(MSVC)
    # clang-cl does not support /MP
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(Lightweight PRIVATE /MP)
    endif()
else()
    set(TEST_LIBRARIES ${TEST_LIBRARIES} odbc ${SQLITE3_LIBRARY}) # FIXME: should be PkgConfig::ODBC in Lightweight target already
endif()

target_link_libraries(LightweightTest PRIVATE ${TEST_LIBRARIES})
# ProfilesLibraryTests.cpp exercises the private detail::Guarded helper directly.
target_include_directories(LightweightTest PRIVATE ${PROJECT_SOURCE_DIR}/src/tools)

# AsynchronousTests.cpp includes stdexec headers directly. Link the shared compat target (root
# CMakeLists) so its SYSTEM includes, /Zc:preprocessor, /external:W0 and -Wno-error demotions apply
# here too — Lightweight links stdexec PRIVATE, so it no longer arrives transitively. Harmless for
# the stdexec-free test TUs.
target_link_libraries(LightweightTest PRIVATE lightweight_stdexec_compat)

if(MSVC)
    target_compile_options(LightweightTest PRIVATE /bigobj)
endif()

set(SOURCE_FILES
    BackupRecordingTests.cpp
    BackupRequirementTests.cpp
    ConfigDiscoveryTests.cpp
    ConfigBackupOutputTests.cpp
    ConfigConnectionTargetTests.cpp
    ConfigLayeredStoreTests.cpp
    ConfigProfileStoreTests.cpp
    ColumnTypeRoundTripTests.cpp
    CoreTests.cpp
    CompositeForeignKeyTests.cpp
    CompositeKeyGapTests.cpp
    CompositeKeyOrderingTests.cpp
    CxxModelPrinterTests.cpp
    CxxModelRelationTests.cpp
    DataBinderTests.cpp
    DataSourceEnumeratorTests.cpp
    Ddl2CppColumnTypeTests.cpp
    PluginDiscoveryTests.cpp
    NoErrorDialogsTests.cpp
    PluginIngestionTests.cpp
    PluginUnloadTests.cpp
    ProfileAddTests.cpp
    ProfileFileEditorTests.cpp
    ProfilePasswordTests.cpp
    ProfileCipherTests.cpp
    ProfilesLibraryTests.cpp
    PreMigrateStepTests.cpp
    ProgressTaskTests.cpp
    UnicodeUpgradeProgressTests.cpp
    SecretResolverTests.cpp
    DataMapper/BatchTests.cpp
    DataMapper/CreateTests.cpp
    DataMapper/EagerLoadingTests.cpp
    DataMapper/MiscTests.cpp
    DataMapper/PoolHealthTests.cpp
    DataMapper/NamingTests.cpp
    DataMapper/ReadTests.cpp
    DataMapper/RowWiseFetchTests.cpp
    DataMapper/RelationShapeTests.cpp
    DataMapper/RelationTests.cpp
    DataMapper/NoexceptContractTests.cpp
    DataMapper/RelationLoadRoutingTests.cpp
    DataMapper/RelationResultTests.cpp
    DataMapper/BelongsToStateTests.cpp
    DataMapper/DescriptorRelationTests.cpp
    DataMapper/InstantiationCoverageTests.cpp
    DataMapper/StateTests.cpp
    DataMapper/ThroughMarkerTests.cpp
    DataMapper/TimestampTests.cpp
    FieldTests.cpp
    Int128Tests.cpp
    LargeTestDatabase/DataGenerator.cpp
    LargeTestDatabase/LargeTestDatabaseTests.cpp
    MigrationTests.cpp
    MigrationReflectionTests.cpp
    QueryBuilderTests.cpp
    QueryFormatterTests.cpp
    SqlErrorDetectionTests.cpp
    SqlErrorSeamTests.cpp
    SqlFaultSeamTests.cpp
    SqlGuidTests.cpp
    SqlLoggerTests.cpp
    SqlPreparedStatementCacheTests.cpp
    SqlStatisticsTests.cpp
    MigrationLockTests.cpp
    SqlConnectionBorrowTests.cpp
    SqlConnectionDbTests.cpp
    SqlBinaryAndTextDbTests.cpp
    SqlConnectInfoEdgeTests.cpp
    SqlDateTimeArithmeticTests.cpp
    SqlDateTimeDbTests.cpp
    SqlDynamicNumericTests.cpp
    SqlNumericTests.cpp
    SqlRealNameTests.cpp
    SqlRetryPolicyTests.cpp
    SqlSchemaDbTests.cpp
    SqlStatementBatchFetchTests.cpp
    SqlStatementPrefetchTests.cpp
    SqlStatementDbTests.cpp
    SqlTransactionTests.cpp
    ThreadSafeQueueTests.cpp
    SqlVariantAccessorTests.cpp
    SqlVariantDbTests.cpp
    SqlBackup/ArchivePathTests.cpp
    SqlBackup/ArchiveVerificationTests.cpp
    SqlBackup/AttachmentsTests.cpp
    SqlBackup/AllColumnTypesTests.cpp
    SqlBackup/EncodingRoundTripTests.cpp
    SqlBackup/RawColumnSliceTests.cpp
    SqlBackup/RestoreCharPaddingTests.cpp
    SqlBackup/RestoreConversionTests.cpp
    SqlBackup/RestoreGuidTextTests.cpp
    SqlBackup/BatchManagerIntegrationTests.cpp
    SqlBackup/ChunkPlannerTests.cpp
    SqlBackup/CommonHelpersTests.cpp
    SqlBackup/ConnectionPoolTests.cpp
    SqlBackup/ProgressManagerTests.cpp
    SqlBackup/RestoreFailureLogTests.cpp
    SqlBackup/RestoreFaultTests.cpp
    SqlBackup/RowIsolationTests.cpp
    SqlBackup/BatchManagerTests.cpp
    SqlBackup/MsgPackChunkFormatsTests.cpp
    SqlBackup/BatchManagerIntegrationTests.cpp
    SqlBackup/BatchManagerTests.cpp
    SqlBackup/MsgPackChunkFormatsTests.cpp
    SqlBackup/MultiPkTests.cpp
    SqlBackup/MultiSchemaTests.cpp
    SqlBackup/Benchmarks.cpp
    SqlBackup/MetadataRedactionTests.cpp
    SqlBackup/ParseSchemaTests.cpp
    SqlBackup/RobustnessTests.cpp
    SqlBackup/Sha256Tests.cpp
    SqlBackup/TableFilterTests.cpp
    SqlBackup/WorkerChunkArchiveTests.cpp
    SqlBackup/Tests.cpp
    SqlBackup/SqlBackupCompositeFKTests.cpp
    SqlBackup/SqlBackupIndexTests.cpp
    SqlBackup/ProductionReadinessTests.cpp
    SqlBackup/BoolColumnTests.cpp
    dbtool/StandardProgressManagerTests.cpp
    dbtool/BackupDiffTests.cpp
    dbtool/BackupRequirementReportTests.cpp
    dbtool/CommandAccessTests.cpp
    dbtool/TaskProgressRendererTests.cpp
    dbtool/UnicodeUpgradeReportTests.cpp
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
    Async/SenderTests.cpp
)

# DocExampleTests.cpp mirrors docs/sql-to-lightweight.md verbatim (enforced by
# scripts/check-doc-snippets.py), so its snippet regions must use the stable C++20 member-pointer
# API (e.g. &Employee::salary) that users actually write — not the reflection-only Member(...) test
# macro. That API is not expressible under the experimental C++26 reflection mode, so exclude this
# translation unit there. It is still compiled and executed against every DBMS in all other builds.
if(NOT LIGHTWEIGHT_CXX26_REFLECTION)
    list(APPEND SOURCE_FILES DocExampleTests.cpp)
endif()

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
add_subdirectory(post_migrate_plugin)
# PluginUnloadTests.cpp loads the post-migrate fixture plugin in-process.
target_compile_definitions(LightweightTest PRIVATE LIGHTWEIGHT_TEST_POST_MIGRATE_PLUGIN="$<TARGET_FILE:post_migrate_plugin>")
add_dependencies(LightweightTest post_migrate_plugin)
add_subdirectory(compat_policy_plugin)
add_subdirectory(seed_row_fixture)
add_subdirectory(gui_workflow_fixtures)

# Register dbtool integration test
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Enable coverage instrumentation and add coverage targets. This must come
# after find_package(Python3): the per-environment coverage targets run the
# dbtool integration suite (test_dbtool.py) via Python3_EXECUTABLE.
if(ENABLE_COVERAGE)
    enable_coverage_for_target(LightweightTest)
    add_coverage_targets(LightweightTest)
endif()
if(WIN32)
    set(SQLITE_DRIVER_NAME "{SQLite3 ODBC Driver}")
else()
    set(SQLITE_DRIVER_NAME "SQLite3")
endif()
add_subdirectory(migration_exit)

add_test(NAME DbToolIntegrationTest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_dbtool.py
        --dbtool $<TARGET_FILE:dbtool>
        --plugins-dir $<TARGET_FILE_DIR:dummy_migration_plugin>
        --connection-string "DRIVER=${SQLITE_DRIVER_NAME};Database=test_dbtool_ctest.db"
        ${LIGHTWEIGHT_TEST_ARGS_LIST}
)

# Script-mode tests for the dbtool master key ring resolution (cmake/DbtoolKeyRing.cmake).
add_test(NAME DbtoolKeyRingConfigTest
    COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/tests/DbtoolKeyRingTest.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# The installed package must provide Lightweight::Profiles to a standalone consumer. The toolchain
# settings of this build are forwarded so the consumer is configured exactly like the library was.
string(REPLACE ";" "|" _profiles_pkg_prefix_path "${CMAKE_PREFIX_PATH}")
# The consumer links the (possibly instrumented) installed library, so it must be built with the
# parent's sanitizer / coverage flags too, or the link or the run would fail.
set(_profiles_pkg_compile_flags ${SANITIZER_COMPILE_OPTIONS})
set(_profiles_pkg_link_flags ${SANITIZER_LINK_OPTIONS})
if(ENABLE_COVERAGE)
    list(APPEND _profiles_pkg_link_flags --coverage)
endif()
list(JOIN _profiles_pkg_compile_flags " " _profiles_pkg_compile_flags)
list(JOIN _profiles_pkg_link_flags " " _profiles_pkg_link_flags)
add_test(NAME ProfilesPackageTest
    COMMAND ${CMAKE_COMMAND}
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "-DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
        "-DTARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}"
        "-DPREFIX_PATH=${_profiles_pkg_prefix_path}"
        "-DBUILD_TYPE=$<CONFIG>"
        "-DLIBDIR=${CMAKE_INSTALL_LIBDIR}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCOMPILE_FLAGS=${_profiles_pkg_compile_flags}"
        "-DLINK_FLAGS=${_profiles_pkg_link_flags}"
        -P ${PROJECT_SOURCE_DIR}/cmake/tests/ProfilesPackageTest.cmake
)

# The Lightweight::Profiles shared library must export nothing but its own namespace (POSIX only:
# Windows exports nothing without __declspec).
if(UNIX)
    add_test(NAME ProfilesExportsTest
        COMMAND ${CMAKE_COMMAND} -DLIB=$<TARGET_FILE:LightweightProfiles> -P ${PROJECT_SOURCE_DIR}/cmake/tests/ProfilesExportsTest.cmake
    )
endif()
