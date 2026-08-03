# dbtool-gui — Qt 6 GUI companion to the dbtool CLI.
#
# Built only when LIGHTWEIGHT_BUILD_GUI is ON and Qt 6 was located by the
# top-level LightweightFindQt probe. See docs/migrations-gui-plan.md for the design.
#
# The C++ view-model layer lives in a STATIC backing library
# (`dbtool-gui-lib`) so the test binaries under `tests/` can link against
# `AppController` & friends without duplicating sources. The executable
# (`dbtool-gui`) reduces to a thin `main.cpp` wrapper around that library.

# The top-level CMakeLists ran `lightweight_probe_qt6()` which extends
# CMAKE_PREFIX_PATH, but did not call `find_package(Qt6)` itself because the
# other project subdirectories (library, dbtool) have no Qt dependency. This
# subdir owns the Qt dependency and pulls the package in explicitly.
# Qt 6.8 is required for `QStyleHints::setColorScheme` (used by main.cpp to
# honour the --theme CLI flag). Bumping the floor keeps the build honest
# about what the source code actually depends on.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Concurrent)

# QuickShapes backs `import QtQuick.Shapes`, used by qml/Glyph.qml to draw the
# GUI's icons as vector paths instead of emoji code points (emoji ignore
# `color`, so they cannot follow the light/dark Theme palette, and their
# metrics differ across the platforms this project's CI matrix builds for).
#
# It is deliberately NOT a REQUIRED component: on Qt 6.8 QuickShapes is still a
# private module, shipping the library and the QtQuick/Shapes QML plugin but no
# public `Qt6QuickShapesConfig.cmake` (only `Qt6QuickShapesPrivate`). Requiring
# it there fails configuration outright. Qt promoted it to a public module in a
# later release, where the target does exist.
#
# Nothing in C++ touches the QuickShapes API — the dependency is QML-only, and
# `import QtQuick.Shapes` resolves from Qt's QML import path at runtime on both
# versions. So we link the public target when it is available (keeping the
# dependency explicit for deployment tooling like windeployqt) and simply skip
# it when it is not. The `dbtool-gui-smoke` test launches the real binary and
# would fail if the QML plugin were genuinely absent.
find_package(Qt6 6.8 QUIET COMPONENTS QuickShapes)

qt_standard_project_setup(REQUIRES 6.8)

set(MIGRATIONS_GUI_CXX_SOURCES
    AppController.hpp
    AppController.cpp
    BackupRunner.hpp
    BackupRunner.cpp
    LogLevel.hpp
    ManagedBackupController.hpp
    ManagedBackupController.cpp
    ManagedBackupCore.hpp
    ManagedBackupCore.cpp
    MigrationRunner.hpp
    MigrationRunner.cpp
    QmlProgressManager.hpp
    QmlProgressManager.cpp
    SqlQueryRunner.hpp
    SqlQueryRunner.cpp
    SqlSyntaxHighlighter.hpp
    SqlSyntaxHighlighter.cpp
    ThemeController.hpp
    ThemeController.cpp

    Models/BackupActiveTablesModel.hpp
    Models/BackupActiveTablesModel.cpp
    Models/BackupProfileFilterModel.hpp
    Models/BackupProfileFilterModel.cpp
    Models/BackupStatusListModel.hpp
    Models/BackupStatusListModel.cpp
    Models/BackupTableListModel.hpp
    Models/BackupTableListModel.cpp
    Models/MigrationListModel.hpp
    Models/MigrationListModel.cpp
    Models/OdbcDataSourceListModel.hpp
    Models/OdbcDataSourceListModel.cpp
    Models/ProfileListModel.hpp
    Models/ProfileListModel.cpp
    Models/ReleaseListModel.hpp
    Models/ReleaseListModel.cpp
    Models/SqlResultModel.hpp
    Models/SqlResultModel.cpp
)

set(MIGRATIONS_GUI_QML_FILES
    qml/Main.qml
    qml/SimpleView.qml
    qml/ExpertView.qml
    qml/Theme.qml
    qml/Glyph.qml
    qml/ProgressTrack.qml
    qml/PageHeader.qml
    qml/ProfileRow.qml
    qml/StatusPill.qml
    qml/StatChip.qml
    qml/MigrationRow.qml
    qml/ReleaseGroup.qml
    qml/MigrationView.qml
    qml/StatusCard.qml
    qml/ReleasesSummary.qml
    qml/ActionsPanel.qml
    qml/BottomPanel.qml
    qml/LogPanel.qml
    qml/SqlQueryPanel.qml
    qml/WheelScrollAmplifier.qml
    qml/ConnectionPanel.qml
    qml/BackupsPage.qml
    qml/BackupDetailPanel.qml
    qml/DestructiveWarningBanner.qml
    qml/SqlPreviewDialog.qml
    qml/ToolBar.qml
    qml/FilterTabs.qml
    qml/BulkControls.qml
    qml/Card.qml
    qml/KineticListView.qml
    qml/TimestampAutocomplete.qml
    qml/PluginsDirField.qml
    qml/SettingsPage.qml
)

# A reusable QML module so the migrations view can be embedded by other Qt
# applications later. URI is `Lightweight.Migrations` (version 1.0).
# Theme.qml is a `pragma Singleton` and must be declared as such in the
# generated qmldir so `Theme` resolves without an explicit instantiation.
# The property MUST be set before `qt_add_qml_module` — the module generator
# reads it when it builds the qmldir, and post-hoc changes are ignored.
set_source_files_properties(qml/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

# STATIC backing library. The QML module is attached to *this* target rather
# than the executable so tests (and any future Qt host application) can link
# `dbtool-gui-lib` and reach both the C++ view-models and the QML files
# through a single dependency.
qt_add_library(dbtool-gui-lib STATIC
    ${MIGRATIONS_GUI_CXX_SOURCES}
)

qt_add_qml_module(dbtool-gui-lib
    URI Lightweight.Migrations
    VERSION 1.0
    QML_FILES ${MIGRATIONS_GUI_QML_FILES}
    RESOURCE_PREFIX /qt/qml
    # Qt expects the module's OUTPUT_DIRECTORY to end with the URI's path
    # (Lightweight/Migrations) so qmllint and related tooling resolve
    # cross-imports without an explicit QML2_IMPORT_PATH. Setting it here
    # silences the "should end in the same target path" warning the
    # auto-derived directory would otherwise produce.
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Lightweight/Migrations
)

target_include_directories(dbtool-gui-lib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Models
)

target_link_libraries(dbtool-gui-lib PUBLIC
    Lightweight
    dbtool_lib  # reuses Tools::PluginLoader for migration-plugin loading
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
)

# Only present on Qt versions where QuickShapes is a public module — see the
# `find_package(... QUIET COMPONENTS QuickShapes)` note at the top of this file.
if(TARGET Qt6::QuickShapes)
    target_link_libraries(dbtool-gui-lib PUBLIC Qt6::QuickShapes)
endif()

enable_coverage_for_target(dbtool-gui-lib)

set_target_properties(dbtool-gui-lib PROPERTIES
    # Qt's camelCase signal/slot convention, unscoped Q_ENUMs, and
    # parent-based `new` ownership all clash with the project-wide
    # clang-tidy policy — opt this target out of clang-tidy entirely.
    CXX_CLANG_TIDY ""
)

# Thin executable: only main.cpp + wiring. All real logic lives in the lib.
qt_add_executable(dbtool-gui
    main.cpp
)

# Windows: supply our own application manifest instead of the one Qt's
# `qt_add_executable` auto-generates. Qt 6.11.x emits `requestedExecutionLevel`
# under a prefixed `asm.v2` namespace, which the Windows activation-context
# loader rejects ("side-by-side configuration is incorrect"). Handing the
# linker a valid manifest overrides Qt's broken default. No-op elsewhere.
if(WIN32)
    target_sources(dbtool-gui PRIVATE dbtool-gui.manifest)
endif()

target_link_libraries(dbtool-gui PRIVATE
    dbtool-gui-lib
    dbtool-gui-libplugin
)

# On Windows release builds, mark as GUI-only (no console window). On debug
# builds we keep a console so Qt warnings, QML error lines, and our own
# stderr output land somewhere the developer can read — the GUI is useless
# if it crashes silently while the event loop is still starting.
# Embed a hand-written application manifest via an .rc RT_MANIFEST resource.
#
# WHY NOT let Qt generate one: qt_add_executable's Windows finaliser
# (_qt_internal_finalize_windows_app) synthesises a `com.yourcompany.*`
# manifest and hands it to the linker through lld-link's
# `/MANIFEST:EMBED /MANIFESTINPUT:`. lld-link's manifest *merge* re-serialises
# `<requestedExecutionLevel>` into the asm.v2 namespace
# (`ms_asmv2:level="asInvoker"`). The Windows SxS loader only recognises that
# attribute in asm.v3, so it reports "the required attribute level is missing
# from element requestedExecutionLevel" and refuses to start the process — the
# GUI never launches. Embedding through an .rc resource copies the manifest
# bytes verbatim (no merge, no re-serialisation), so the namespaces survive.
#
# Providing our own `.manifest` in the target sources also trips Qt's
# "Skipping manifest magic due to user specified manifest file" branch, so Qt
# stops generating the broken one. See src/tools/dbtool-gui/dbtool-gui.manifest.
if(WIN32)
    target_sources(dbtool-gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/dbtool-gui.manifest
        ${CMAKE_CURRENT_SOURCE_DIR}/dbtool-gui.rc
    )
    # The .manifest is present ONLY to trip Qt's "user specified manifest file"
    # skip branch. It must NOT be fed to the linker as a /MANIFESTINPUT (that is
    # the merge path that corrupts the namespaces) — the .rc embeds it instead.
    # HEADER_FILE_ONLY keeps CMake from treating it as a manifest link input.
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/dbtool-gui.manifest
        PROPERTIES HEADER_FILE_ONLY TRUE
    )
    # Belt-and-braces: keep the linker from also generating/merging a manifest.
    target_link_options(dbtool-gui PRIVATE
        $<$<CXX_COMPILER_ID:MSVC,Clang>:/MANIFEST:NO>
    )
endif()

set_target_properties(dbtool-gui PROPERTIES
    WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
    MACOSX_BUNDLE ON
    CXX_CLANG_TIDY ""
)

install(TARGETS dbtool-gui
    BUNDLE  DESTINATION .                          COMPONENT Runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}    COMPONENT Runtime
)

# Install-time Qt deployment: the POST_BUILD windeployqt below populates the
# build tree, but the install staging tree is a separate directory. Run it
# again at install time so the MSI / DEB / RPM payload contains all Qt6 DLLs,
# QML modules, platform plugins, styles, and image formats.
if(WIN32)
    install(CODE "
        set(_qml_dir \"${CMAKE_CURRENT_SOURCE_DIR}/qml\")
        set(_target  \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/dbtool-gui.exe\")
        if(NOT EXISTS \"\${_target}\")
            message(FATAL_ERROR \"windeployqt: \${_target} not found in install tree\")
        endif()
        find_program(_windeployqt windeployqt HINTS \"${Qt6_DIR}/../../../bin\")
        if(NOT _windeployqt)
            message(FATAL_ERROR \"windeployqt not found; cannot stage Qt runtime for dbtool-gui\")
        endif()
        set(_cfg_flag --release)
        if(\"\${CMAKE_INSTALL_CONFIG_NAME}\" STREQUAL \"Debug\")
            set(_cfg_flag --debug)
        endif()
        message(STATUS \"Running windeployqt against \${_target} (\${_cfg_flag})\")
        execute_process(COMMAND \"\${_windeployqt}\"
            \${_cfg_flag}
            --no-translations
            --no-system-d3d-compiler
            --qmldir \"\${_qml_dir}\"
            --dir    \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\"
            \"\${_target}\"
            RESULT_VARIABLE _wdq_rc
        )
        if(NOT _wdq_rc EQUAL 0)
            message(FATAL_ERROR \"windeployqt failed with exit code \${_wdq_rc}\")
        endif()
    " COMPONENT Runtime)
endif()

# Post-build deployment hooks so a fresh checkout can run the binary without
# manually copying Qt DLLs / frameworks. No-ops on Linux (use system Qt or
# AppImage out-of-tree).
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET dbtool-gui POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    "$<TARGET_FILE:dbtool-gui>"
            COMMENT "Running windeployqt for dbtool-gui"
            VERBATIM
        )
    else()
        message(STATUS "windeployqt not found — skipping Qt DLL deployment for dbtool-gui")
    endif()
elseif(APPLE)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(MACDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET dbtool-gui POST_BUILD
            COMMAND "${MACDEPLOYQT_EXECUTABLE}"
                    "$<TARGET_BUNDLE_DIR:dbtool-gui>"
                    -qmldir=${CMAKE_CURRENT_SOURCE_DIR}/qml
            COMMENT "Running macdeployqt for dbtool-gui"
            VERBATIM
        )
    else()
        message(STATUS "macdeployqt not found — skipping Qt framework deployment for dbtool-gui")
    endif()
endif()

if(LIGHTWEIGHT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
