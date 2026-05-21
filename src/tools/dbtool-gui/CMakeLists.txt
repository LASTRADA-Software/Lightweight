# dbtool-gui — Qt 6 GUI companion to the dbtool CLI.
#
# Built only when LIGHTWEIGHT_BUILD_GUI is ON and Qt 6 was located by the
# top-level LightweightFindQt probe. See docs/migrations-gui-plan.md for the design.

# The top-level CMakeLists ran `lightweight_probe_qt6()` which extends
# CMAKE_PREFIX_PATH, but did not call `find_package(Qt6)` itself because the
# other project subdirectories (library, dbtool) have no Qt dependency. This
# subdir owns the Qt dependency and pulls the package in explicitly.
# Qt 6.8 is required for `QStyleHints::setColorScheme` (used by main.cpp to
# honour the --theme CLI flag). Bumping the floor keeps the build honest
# about what the source code actually depends on.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Concurrent)

qt_standard_project_setup(REQUIRES 6.8)

set(MIGRATIONS_GUI_CXX_SOURCES
    main.cpp

    AppController.hpp
    AppController.cpp
    BackupRunner.hpp
    BackupRunner.cpp
    LogLevel.hpp
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
    qml/StatusPill.qml
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
    qml/BackupRestoreDialog.qml
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

qt_add_executable(dbtool-gui
    ${MIGRATIONS_GUI_CXX_SOURCES}
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

qt_add_qml_module(dbtool-gui
    URI Lightweight.Migrations
    VERSION 1.0
    QML_FILES ${MIGRATIONS_GUI_QML_FILES}
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(dbtool-gui PRIVATE
    Lightweight
    dbtool_lib  # reuses Tools::PluginLoader for migration-plugin loading
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
)

# On Windows release builds, mark as GUI-only (no console window). On debug
# builds we keep a console so Qt warnings, QML error lines, and our own
# stderr output land somewhere the developer can read — the GUI is useless
# if it crashes silently while the event loop is still starting.
set_target_properties(dbtool-gui PROPERTIES
    WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
    MACOSX_BUNDLE ON
    # Qt's camelCase signal/slot convention, unscoped Q_ENUMs, and
    # parent-based `new` ownership all clash with the project-wide
    # clang-tidy policy — opt this target out of clang-tidy entirely.
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
