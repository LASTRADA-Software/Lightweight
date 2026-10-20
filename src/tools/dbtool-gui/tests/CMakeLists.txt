# dbtool-gui — Catch2 unit tests for the C++ view-model layer.
#
# Links against `dbtool-gui-lib` so the same `AppController`, `MigrationRunner`,
# and list-model objects driven by the QML UI are exercised directly from
# C++. The tests run under `QT_QPA_PLATFORM=offscreen` and do not require a
# display server — see the parent `CMakeLists.txt` for the build gate.

find_package(Catch2 REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)

qt_add_executable(dbtool-gui-tests
    main.cpp
    AppControllerTests.cpp
    BackupActiveTablesModelTests.cpp
    BackupProfileFilterModelTests.cpp
    BackupRequirementControllerTests.cpp
    BackupRunnerTests.cpp
    BackupStatusListModelTests.cpp
    BackupTableListModelTests.cpp
    CloseGuardTests.cpp
    FileWriteTests.cpp
    FixturePluginTests.cpp
    MigrationListModelCountTests.cpp
    MigrationPlanModelTests.cpp
    MigrationRunnerLockingTests.cpp
    MigrationRunnerNotificationTests.cpp
    ManagedBackupCoreTests.cpp
    ManagedBackupControllerTests.cpp
    MigrationRunnerPostMigrateTests.cpp
    MigrationRunnerPreMigrateTests.cpp
    NoErrorDialogsTests.cpp
    ProgressTaskModelTests.cpp
    RunnerBusyGuardTests.cpp
    UiWatchdogTests.cpp
    support/UiWatchdog.hpp
    support/UiWatchdog.cpp
)

target_link_libraries(dbtool-gui-tests PRIVATE
    dbtool-gui-lib
    dbtool-gui-libplugin
    Catch2::Catch2
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Test
)

# Fixture plugins from src/tests: the compat-policy one, which the plugin-reload test loads, and
# the backup-required one, which the backup-requirement tests load.
target_compile_definitions(dbtool-gui-tests PRIVATE
    DBTOOL_GUI_TESTS_COMPAT_POLICY_FIXTURE="$<TARGET_FILE:compat_policy_fixture>"
    DBTOOL_GUI_TESTS_BACKUP_REQUIRED_FIXTURE="$<TARGET_FILE:backup_required_release_fixture>"
)
add_dependencies(dbtool-gui-tests compat_policy_fixture backup_required_release_fixture)

set_target_properties(dbtool-gui-tests PROPERTIES
    # Qt's camelCase signal/slot convention clashes with the project-wide
    # clang-tidy policy — opt out, like the executable does.
    CXX_CLANG_TIDY ""
)

# The post-migrate recorder fixture, whose post-init hook registers a pre-migrate step; the
# plugin-reload test checks that a reload while connected keeps it.
target_compile_definitions(dbtool-gui-tests PRIVATE
    DBTOOL_GUI_TESTS_PRE_MIGRATE_RECORDER_PLUGIN="$<TARGET_FILE:post_migrate_plugin>"
)
add_dependencies(dbtool-gui-tests post_migrate_plugin)

# The fixture plugins of the workflow tests, by the keys `WorkflowFixture::newPluginsDir` takes, and
# the dbtool CLI the workflow tests run. Shared by `dbtool-gui-tests` and `dbtool-gui-workflowtest`.
set(DBTOOL_GUI_WORKFLOW_FIXTURE_TARGETS
    gui_workflow_fixture
    post_migrate_plugin
    backup_required_release_fixture
    migration_requires_backup_fixture
    unicode_candidate_fixture
    failing_migration_fixture
    bulk_migrations_fixture
)
set(DBTOOL_GUI_WORKFLOW_FIXTURE_DEFINITIONS
    DBTOOL_GUI_WORKFLOW_FIXTURE_WORKFLOW="$<TARGET_FILE:gui_workflow_fixture>"
    DBTOOL_GUI_WORKFLOW_FIXTURE_RECORDER="$<TARGET_FILE:post_migrate_plugin>"
    DBTOOL_GUI_WORKFLOW_FIXTURE_BACKUP_REQUIRED_RELEASE="$<TARGET_FILE:backup_required_release_fixture>"
    DBTOOL_GUI_WORKFLOW_FIXTURE_MIGRATION_REQUIRES_BACKUP="$<TARGET_FILE:migration_requires_backup_fixture>"
    DBTOOL_GUI_WORKFLOW_FIXTURE_UNICODE_CANDIDATE="$<TARGET_FILE:unicode_candidate_fixture>"
    DBTOOL_GUI_WORKFLOW_FIXTURE_FAILING="$<TARGET_FILE:failing_migration_fixture>"
    DBTOOL_GUI_WORKFLOW_FIXTURE_BULK="$<TARGET_FILE:bulk_migrations_fixture>"
)
target_compile_definitions(dbtool-gui-tests PRIVATE ${DBTOOL_GUI_WORKFLOW_FIXTURE_DEFINITIONS})
add_dependencies(dbtool-gui-tests ${DBTOOL_GUI_WORKFLOW_FIXTURE_TARGETS})

# Embed a hand-written application manifest via an .rc RT_MANIFEST resource,
# exactly like the `dbtool-gui` executable does (see ../CMakeLists.txt and
# commit f60871ff for the full write-up). Without this, qt_add_executable's
# Windows finaliser lets lld-link generate and *merge* its own manifest,
# which re-serialises <requestedExecutionLevel> into the asm.v2 namespace.
# The Windows SxS loader only accepts that attribute in asm.v3, so it refuses
# to start the process at all — the test binary never even reaches main().
# Embedding through an .rc resource instead copies the manifest bytes
# verbatim, bypassing the broken merge path. The .rc reuses the SAME
# ../dbtool-gui.manifest file (no test-specific content needed).
if(WIN32)
    target_sources(dbtool-gui-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../dbtool-gui.manifest
        ${CMAKE_CURRENT_SOURCE_DIR}/dbtool-gui-tests.rc
    )
    # HEADER_FILE_ONLY keeps CMake from feeding the .manifest to the linker
    # as a /MANIFESTINPUT (that's the merge path that corrupts the
    # namespaces) — the .rc embeds it instead. Its mere presence in
    # target_sources still trips Qt's "user specified manifest file" skip
    # branch, so Qt stops generating its own broken one.
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/../dbtool-gui.manifest
        PROPERTIES HEADER_FILE_ONLY TRUE
    )
    # Belt-and-braces: keep the linker from also generating/merging a manifest.
    target_link_options(dbtool-gui-tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC,Clang>:/MANIFEST:NO>
    )
endif()

# Stage the Qt runtime next to the test binary. windeployqt deploys only the
# `windows` platform plugin by default, so `--include-plugins qoffscreen` adds
# the one `QT_QPA_PLATFORM=offscreen` needs to run without a display server.
# The same invocation copies QML modules, image format plugins, etc.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET dbtool-gui-tests POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --include-plugins qoffscreen
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/../qml"
                    "$<TARGET_FILE:dbtool-gui-tests>"
            COMMENT "Running windeployqt for dbtool-gui-tests"
            VERBATIM
        )
    endif()
endif()

add_test(NAME dbtool-gui-tests
    COMMAND dbtool-gui-tests
)

set_tests_properties(dbtool-gui-tests PROPERTIES
    LABELS "dbtool-gui"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# QML component layer. Drives the `Lightweight.Migrations` QML module through
# Qt's `qmltestrunner`, executing every `tst_*.qml` file under `qml/` as a
# separate test function. Catches binding / singleton-registration regressions
# the C++ harness cannot reach.
qt_add_executable(dbtool-gui-qmltest
    qmltest_main.cpp
)

target_link_libraries(dbtool-gui-qmltest PRIVATE
    Qt6::QuickTest
    Qt6::Quick
    Qt6::Qml
    dbtool-gui-lib
    dbtool-gui-libplugin
)

set_target_properties(dbtool-gui-qmltest PROPERTIES
    CXX_CLANG_TIDY ""
)

# Same lld-link SxS manifest-merge bug as `dbtool-gui-tests` above (and the
# `dbtool-gui` executable, commit f60871ff) — embed the shared manifest via
# .rc RT_MANIFEST instead of letting Qt/lld-link generate+merge a corrupted
# one, or this executable will refuse to start with a side-by-side
# configuration error too.
if(WIN32)
    target_sources(dbtool-gui-qmltest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../dbtool-gui.manifest
        ${CMAKE_CURRENT_SOURCE_DIR}/dbtool-gui-tests.rc
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/../dbtool-gui.manifest
        PROPERTIES HEADER_FILE_ONLY TRUE
    )
    target_link_options(dbtool-gui-qmltest PRIVATE
        $<$<CXX_COMPILER_ID:MSVC,Clang>:/MANIFEST:NO>
    )
endif()

# Stage the Qt runtime, including the offscreen platform plugin, next to the
# qmltest binary so it can launch under `QT_QPA_PLATFORM=offscreen`.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET dbtool-gui-qmltest POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --include-plugins qoffscreen
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/../qml"
                    "$<TARGET_FILE:dbtool-gui-qmltest>"
            COMMENT "Running windeployqt for dbtool-gui-qmltest"
            VERBATIM
        )
    endif()
endif()

add_test(NAME dbtool-gui-qmltest
    COMMAND dbtool-gui-qmltest -input ${CMAKE_CURRENT_SOURCE_DIR}/qml
)

set_tests_properties(dbtool-gui-qmltest PROPERTIES
    LABELS "dbtool-gui"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# GUI workflow tests: the real Main window driven through its controls by Qt Quick Test, with the
# `DbtoolGuiWorkflow` helpers (WorkflowFixture, UiWatchdog) registered into QML. One CTest, and with
# it one process and one AppController, per test function of a `workflow/tst_workflow_*.qml` file.
qt_add_executable(dbtool-gui-workflowtest
    workflow/workflowtest_main.cpp
    support/UiWatchdog.hpp
    support/UiWatchdog.cpp
    support/DbAccessProbe.hpp
    support/DbAccessProbe.cpp
    support/WorkflowFixture.hpp
    support/WorkflowFixture.cpp
    support/WorkflowSetup.hpp
)

target_link_libraries(dbtool-gui-workflowtest PRIVATE
    Qt6::QuickTest
    Qt6::Quick
    Qt6::Qml
    Qt6::Gui
    dbtool-gui-lib
    dbtool-gui-libplugin
)

# User DSNs for the DSN-mode workflow tests (SQLConfigDataSourceW); odbccp32 still needs the legacy stdio symbols.
if(WIN32)
    target_link_libraries(dbtool-gui-workflowtest PRIVATE odbccp32 legacy_stdio_definitions)
endif()

target_compile_definitions(dbtool-gui-workflowtest PRIVATE
    ${DBTOOL_GUI_WORKFLOW_FIXTURE_DEFINITIONS}
    DBTOOL_GUI_WORKFLOW_DBTOOL="$<TARGET_FILE:dbtool>"
)
add_dependencies(dbtool-gui-workflowtest ${DBTOOL_GUI_WORKFLOW_FIXTURE_TARGETS} dbtool)

set_target_properties(dbtool-gui-workflowtest PROPERTIES
    CXX_CLANG_TIDY ""
)

# Same lld-link SxS manifest-merge bug as `dbtool-gui-tests` above: embed the shared manifest via .rc.
if(WIN32)
    target_sources(dbtool-gui-workflowtest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../dbtool-gui.manifest
        ${CMAKE_CURRENT_SOURCE_DIR}/dbtool-gui-tests.rc
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/../dbtool-gui.manifest
        PROPERTIES HEADER_FILE_ONLY TRUE
    )
    target_link_options(dbtool-gui-workflowtest PRIVATE
        $<$<CXX_COMPILER_ID:MSVC,Clang>:/MANIFEST:NO>
    )
endif()

# Stage the Qt runtime, including the offscreen platform plugin, next to the binary.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET dbtool-gui-workflowtest POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --include-plugins qoffscreen
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/../qml"
                    "$<TARGET_FILE:dbtool-gui-workflowtest>"
            COMMENT "Running windeployqt for dbtool-gui-workflowtest"
            VERBATIM
        )
    endif()
endif()

# The AppController is a process-wide singleton, so each test function gets a process of its own:
# the CTest `dbtool-gui-workflow-<file stem>-<function>` runs `<TestCase name>::<function>` only.
file(GLOB DBTOOL_GUI_WORKFLOW_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/workflow/tst_workflow_*.qml)
foreach(workflowFile IN LISTS DBTOOL_GUI_WORKFLOW_FILES)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${workflowFile})
    get_filename_component(workflowStem ${workflowFile} NAME_WE)
    file(STRINGS ${workflowFile} workflowNameLines REGEX "^    name: \"[A-Za-z0-9_]+\"")
    if(NOT workflowNameLines MATCHES "name: \"([A-Za-z0-9_]+)\"")
        message(FATAL_ERROR "${workflowFile} needs a `name: \"...\"` line at the top level of its WorkflowTestCase")
    endif()
    set(workflowCaseName ${CMAKE_MATCH_1})
    # `_data` functions feed data-driven tests and are no tests themselves. A test function written
    # any other way than the strict form would silently run nowhere, so the two counts must agree.
    file(STRINGS ${workflowFile} workflowFunctionLines REGEX "^    function test_[A-Za-z0-9_]+[(]")
    list(FILTER workflowFunctionLines EXCLUDE REGEX "_data[(]")
    file(STRINGS ${workflowFile} workflowLooseLines REGEX "function[ \t]+test_")
    list(FILTER workflowLooseLines EXCLUDE REGEX "function[ \t]+test_[A-Za-z0-9_]*_data[ \t]*[(]")
    list(LENGTH workflowFunctionLines workflowStrictCount)
    list(LENGTH workflowLooseLines workflowLooseCount)
    if(NOT workflowStrictCount EQUAL workflowLooseCount)
        message(FATAL_ERROR "${workflowFile}: ${workflowLooseCount} test functions, but only ${workflowStrictCount} "
                            "in the form `    function test_<name>(` (4-space indent) that registers a CTest")
    endif()
    foreach(functionLine IN LISTS workflowFunctionLines)
        string(REGEX MATCH "test_[A-Za-z0-9_]+" workflowFunction "${functionLine}")
        add_test(NAME dbtool-gui-workflow-${workflowStem}-${workflowFunction}
            COMMAND dbtool-gui-workflowtest -input ${workflowFile} ${workflowCaseName}::${workflowFunction}
        )
        set_tests_properties(dbtool-gui-workflow-${workflowStem}-${workflowFunction} PROPERTIES
            LABELS "dbtool-gui"
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LIGHTWEIGHT_NO_ERROR_DIALOGS=1"
        )
    endforeach()
endforeach()

# Process-level smoke test driven from Python: launches the real `dbtool-gui`
# binary under the offscreen platform plugin and asserts it stays alive
# without any fatal log lines. This catches a class of failure the Catch2
# and qmltestrunner suites cannot reach: a broken QML loader, missing
# resources after windeployqt, or a regression in `main.cpp` itself.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME dbtool-gui-smoke
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/src/tests/test_dbtool_gui.py
                --dbtool-gui $<TARGET_FILE:dbtool-gui>
    )
    set_tests_properties(dbtool-gui-smoke PROPERTIES
        LABELS "dbtool-gui"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
