# Lightweight — packaging (CPack)
#
# Drives all generators:
#   - Windows: WIX (MSI)
#   - Linux  : TGZ + DEB + RPM
#   - macOS  : TGZ (placeholder, not exercised)
#
# Components:
#   - Runtime     : dbtool, dbtool-gui, Lightweight.dll, runtime DLLs, Qt deployment
#   - Development : headers, import libs, CMake config files
#
# End-user installers (WIX, DEB, RPM) ship only the Runtime component. The
# Development component is reachable via `cmake --install --component Development`.

include(GNUInstallDirs)

# --- Common metadata -------------------------------------------------------

set(CPACK_PACKAGE_NAME                "Lightweight")
set(CPACK_PACKAGE_VERSION             "${LIGHTWEIGHT_VERSION}")
set(CPACK_PACKAGE_VENDOR              "LASTRADA Software")
set(CPACK_PACKAGE_HOMEPAGE_URL        "https://github.com/LASTRADA-Software/Lightweight")
set(CPACK_PACKAGE_CONTACT             "Christian Parpart <christian@parpart.family>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight SQL C++ library on top of ODBC")
set(CPACK_RESOURCE_FILE_README        "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE       "${CMAKE_SOURCE_DIR}/cmake/license.rtf")
set(CPACK_PACKAGE_INSTALL_DIRECTORY   "Lightweight")

# --- Component layout ------------------------------------------------------

set(CPACK_COMPONENTS_GROUPING ONE_PER_GROUP)
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME     "Lightweight Tools")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION      "dbtool CLI and dbtool-gui (Qt) plus required runtime libraries.")
set(CPACK_COMPONENT_RUNTIME_REQUIRED         TRUE)
set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "Lightweight SDK")
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION  "C++ headers, import libraries, and CMake package files for downstream use of the Lightweight library.")
set(CPACK_COMPONENT_DEVELOPMENT_DISABLED     TRUE)

# --- Per-generator selection ----------------------------------------------

if(WIN32)
    if(NOT CPACK_GENERATOR)
        set(CPACK_GENERATOR "WIX")
    endif()
    # End-user MSI: Runtime only. Development can still be staged via
    # `cmake --install --component Development` for downstream library consumers.
    set(CPACK_COMPONENTS_ALL Runtime)
elseif(UNIX AND NOT APPLE)
    if(NOT CPACK_GENERATOR)
        set(CPACK_GENERATOR "TGZ;DEB;RPM")
    endif()
    set(CPACK_COMPONENTS_ALL Runtime)
else()
    if(NOT CPACK_GENERATOR)
        set(CPACK_GENERATOR "TGZ")
    endif()
    set(CPACK_COMPONENTS_ALL Runtime)
endif()

# --- WiX (Windows MSI) -----------------------------------------------------

if(WIN32)
    # Stable upgrade GUID. NEVER regenerate — without a stable GUID,
    # consecutive installer versions cannot upgrade each other in place.
    set(CPACK_WIX_UPGRADE_GUID "A059EBE6-C185-4762-9B33-4B29FA079477")

    # WiX 4+ uses the unified `wix.exe` binary. CMake's CPack WIX module accepts
    # only the values "3" or "4" — WiX 5 is API-compatible with WiX 4 and is
    # driven through the same `wix.exe` interface, so we keep this at 4.
    set(CPACK_WIX_VERSION 4)
    if(NOT CPACK_WIX_ROOT)
        foreach(_root "C:/Program Files/WiX Toolset v5.0"
                      "C:/Program Files/WiX Toolset v4.0")
            if(EXISTS "${_root}/bin/wix.exe")
                set(CPACK_WIX_ROOT "${_root}")
                break()
            endif()
        endforeach()
    endif()

    set(CPACK_WIX_PROGRAM_MENU_FOLDER "Lightweight")
    set(CPACK_WIX_PROPERTY_ARPHELPLINK    "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")

    # Start Menu shortcut for the GUI. dbtool (CLI) gets no shortcut — it is
    # invoked from a shell. Format: "<target-name>;<friendly-label>".
    set(CPACK_PACKAGE_EXECUTABLES "dbtool-gui;Lightweight dbtool GUI")

    # Pull in the redistributable VC++ runtime so the MSI can land on a clean
    # machine without a separate vcredist install. UCRT is on by default.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Runtime)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    include(InstallRequiredSystemLibraries)
endif()

# --- DEB (Debian / Ubuntu) -------------------------------------------------

if(UNIX AND NOT APPLE)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER      "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION         "database")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY        "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE        "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS       ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "unixodbc, libzip4 | libzip5, libyaml-cpp0.7 | libyaml-cpp0.8, libqt6core6, libqt6gui6, libqt6qml6, libqt6quick6, libqt6quickcontrols2-6")
    set(CPACK_DEB_COMPONENT_INSTALL ON)
endif()

# --- RPM (Fedora / RHEL / openSUSE) ----------------------------------------

if(UNIX AND NOT APPLE)
    set(CPACK_RPM_PACKAGE_LICENSE  "Apache-2.0")
    set(CPACK_RPM_PACKAGE_GROUP    "Applications/Databases")
    set(CPACK_RPM_PACKAGE_URL      "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_RPM_PACKAGE_VENDOR   "${CPACK_PACKAGE_VENDOR}")
    set(CPACK_RPM_PACKAGE_AUTOREQ  ON)
    set(CPACK_RPM_PACKAGE_REQUIRES "unixODBC, libzip, yaml-cpp, qt6-qtbase, qt6-qtdeclarative")
    set(CPACK_RPM_COMPONENT_INSTALL ON)
endif()

include(CPack)

# Component declarations must come after include(CPack).
cpack_add_component(Runtime
    DISPLAY_NAME "Lightweight Tools"
    DESCRIPTION  "dbtool CLI and dbtool-gui (Qt) plus required runtime libraries."
    REQUIRED
)
cpack_add_component(Development
    DISPLAY_NAME "Lightweight SDK"
    DESCRIPTION  "C++ headers, import libraries, and CMake package files."
    DISABLED
)
