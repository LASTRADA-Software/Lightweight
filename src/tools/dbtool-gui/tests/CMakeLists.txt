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
    BackupStatusListModelTests.cpp
    BackupTableListModelTests.cpp
    ManagedBackupCoreTests.cpp
    ManagedBackupControllerTests.cpp
    RunnerBusyGuardTests.cpp
)

target_link_libraries(dbtool-gui-tests PRIVATE
    dbtool-gui-lib
    dbtool-gui-libplugin
    Catch2::Catch2
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

set_target_properties(dbtool-gui-tests PROPERTIES
    # Qt's camelCase signal/slot convention clashes with the project-wide
    # clang-tidy policy — opt out, like the executable does.
    CXX_CLANG_TIDY ""
)

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

# Stage the Qt offscreen + minimal platform plugins next to the test binary
# so `QT_QPA_PLATFORM=offscreen` resolves without a display server. The same
# windeployqt invocation copies QML modules, image format plugins, etc.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET dbtool-gui-tests POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
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

# Stage the Qt platform plugins next to the qmltest binary so it can launch
# under `QT_QPA_PLATFORM=offscreen` from a clean install.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET dbtool-gui-qmltest POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
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
