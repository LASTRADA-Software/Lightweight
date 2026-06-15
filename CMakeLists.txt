cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Version)
GetVersionInformation(LIGHTWEIGHT_VERSION LIGHTWEIGHT_VERSION_STRING)
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${LIGHTWEIGHT_VERSION_STRING}")
project(Lightweight VERSION "${LIGHTWEIGHT_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

include(ClangTidy)
include(PedanticCompiler)
include(Sanitizers)
include(Coverage)
include(GNUInstallDirs)

find_package(reflection-cpp 0.4.0 QUIET)
find_package(Catch2 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG QUIET)

option(LIGHTWEIGHT_CXX26_REFLECTION "Enable experimental reflection support from C++26" OFF)
option(LIGHTWEIGHT_BUILD_MODULES "Build Lightweight as C++20 modules (requires CMake 3.28+)" OFF)

# Shared library default: ON for Windows (DLL), OFF for other platforms
set(LIGHTWEIGHT_BUILD_SHARED_DEFAULT OFF)
if(WIN32)
    set(LIGHTWEIGHT_BUILD_SHARED_DEFAULT ON)
endif()
option(LIGHTWEIGHT_BUILD_SHARED "Build Lightweight as a shared library" ${LIGHTWEIGHT_BUILD_SHARED_DEFAULT})

# Component build options
option(LIGHTWEIGHT_BUILD_TESTS "Build Lightweight tests" ON)
option(LIGHTWEIGHT_BUILD_TOOLS "Build Lightweight tools" ON)
option(LIGHTWEIGHT_BUILD_EXAMPLES "Build Lightweight examples" ON)
option(LIGHTWEIGHT_BUILD_BENCHMARK "Build Lightweight benchmark" OFF)

# Auto-enable large-db-generator in Debug builds by default
set(LIGHTWEIGHT_BUILD_LARGEDB_TOOL_DEFAULT OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LIGHTWEIGHT_BUILD_LARGEDB_TOOL_DEFAULT ON)
endif()
option(LIGHTWEIGHT_BUILD_LARGEDB_TOOL "Build large database generator tool" ${LIGHTWEIGHT_BUILD_LARGEDB_TOOL_DEFAULT})
unset(LIGHTWEIGHT_BUILD_LARGEDB_TOOL_DEFAULT)

# GUI tool (Qt 6) — opt-in. If ON we auto-probe common Qt install locations
# (C:/Qt, Homebrew qt@6, ~/Qt) and fall back gracefully if nothing is found.
option(LIGHTWEIGHT_BUILD_GUI "Build the Qt 6 dbtool GUI (src/tools/dbtool-gui)" OFF)

option(LIGHTWEIGHT_BUILD_DOCUMENTATION "Create and install the HTML based API documentation (requires Doxygen)" OFF)
option(LIGHTWEIGHT_DOCS_WARN_AS_ERROR "Treat Doxygen warnings as errors" OFF)

# Optional Tracy profiler integration. When ON, fetches Tracy
# via vcpkg if available, otherwise via CPM
option(LIGHTWEIGHT_ENABLE_TRACY "Enable Tracy profiler instrumentation in the Lightweight library and downstream code" OFF)

if(LIGHTWEIGHT_BUILD_MODULES)
    if(CMAKE_VERSION VERSION_LESS "3.28")
        message(FATAL_ERROR "C++20 module support requires CMake 3.28 or higher. Current version: ${CMAKE_VERSION}")
    endif()
    message(STATUS "C++20 module build enabled.")
endif()

if(LIGHTWEIGHT_CXX26_REFLECTION)
    add_definitions(-DLIGHTWEIGHT_CXX26_REFLECTION)
    set(CMAKE_CXX_STANDARD 26)
    message(STATUS "C++26 reflection support enabled.")
endif()

set(CPM_DOWNLOAD_VERSION 0.40.2)
if(CPM_SOURCE_CACHE)
    set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
    set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD
         https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
         ${CPM_DOWNLOAD_LOCATION}
    )
endif()

include(${CPM_DOWNLOAD_LOCATION})

if(reflection-cpp_FOUND)
    message(STATUS "reflection-cpp found: ${reflection-cpp_INCLUDE_DIRS}")
else()
    message(STATUS "reflection-cpp not found, downloading...")
    CPMAddPackage(
        NAME reflection-cpp
        GITHUB_REPOSITORY contour-terminal/reflection-cpp
        GIT_TAG master
        EXCLUDE_FROM_ALL YES
        SYSTEM YES
    )
endif()

# stdexec (NVIDIA's reference implementation of std::execution / P2300 — the C++26 executors).
# Mandatory, first-class PUBLIC dependency of the Lightweight async layer. Resolve via
# find_package (a sufficiently new system/vcpkg install) first, falling back to CPM exactly like
# reflection-cpp/libzip. stdexec's core (static_thread_pool, async_scope, run_loop, schedulers) is
# header-only and does NOT need TBB — TBB is only used by stdexec's parallel algorithms.
#
# Single source of truth for the stdexec version. LIGHTWEIGHT_STDEXEC_GIT_TAG is the CPM
# fallback tag; LIGHTWEIGHT_STDEXEC_MIN_VERSION is the floor find_package must satisfy, kept in
# lockstep so anything older than the CPM tag's API contract is rejected. The minimum exists
# because the stdexec 0.8.0 that current vcpkg baselines ship has a broken static_thread_pool
# execution domain on MSVC (its domain lacks `apply_sender`, so sync_wait / spawn / start_detached
# do not compile against the pool) and predates the `starts_on` adaptor; 0.9.0+ fixes this. That
# is also why stdexec is deliberately NOT listed in vcpkg.json: installing the 0.8.0 port would
# put its headers on the global vcpkg include path ahead of the CPM copy and shadow it. find_package
# still prefers a usable system/vcpkg stdexec when one is present; once a vcpkg baseline ships a
# new-enough port, re-add it to vcpkg.json and it is used automatically with no other change.
set(LIGHTWEIGHT_STDEXEC_GIT_TAG "nvhpc-26.05" CACHE STRING
    "Git tag/branch of NVIDIA/stdexec fetched via CPM when no suitable system/vcpkg package is found")
set(LIGHTWEIGHT_STDEXEC_MIN_VERSION "0.9.0" CACHE STRING
    "Minimum stdexec version accepted from find_package (older has a broken static_thread_pool domain on MSVC)")

set(STDEXEC_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(STDEXEC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(STDEXEC_ENABLE_TBB     OFF CACHE BOOL "" FORCE)

find_package(stdexec ${LIGHTWEIGHT_STDEXEC_MIN_VERSION} CONFIG QUIET)
if(stdexec_FOUND)
    message(STATUS "stdexec ${stdexec_VERSION} found via find_package (vcpkg/system)")
else()
    message(STATUS "stdexec (>= ${LIGHTWEIGHT_STDEXEC_MIN_VERSION}) not found, "
                   "downloading ${LIGHTWEIGHT_STDEXEC_GIT_TAG} via CPM...")
    CPMAddPackage(
        NAME stdexec
        GITHUB_REPOSITORY NVIDIA/stdexec
        GIT_TAG ${LIGHTWEIGHT_STDEXEC_GIT_TAG}
        EXCLUDE_FROM_ALL YES
        SYSTEM YES
    )
    # stdexec's own `system_context` target inherits this project's pedantic flags under
    # EXCLUDE_FROM_ALL; demote the warnings it triggers so PEDANTIC_COMPILER_WERROR builds it.
    # (CPM build-from-source path only — the vcpkg/system path ships a prebuilt config package.)
    if(TARGET system_context)
        target_compile_options(system_context PRIVATE
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=empty-body>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-error=missing-declarations>
            $<$<CXX_COMPILER_ID:Clang>:-Wno-error=newline-eof>
            $<$<CXX_COMPILER_ID:Clang>:-Wno-error=macro-redefined>
        )
    endif()
endif()

# Treat stdexec's headers as SYSTEM includes for every consumer, on both resolution paths. On
# GCC/Clang this maps to -isystem and suppresses header-origin warnings under -Werror; on MSVC it
# maps to /external:I (see the /external:W0 mitigation on the Lightweight target). Idempotent if
# the includes are already SYSTEM.
if(TARGET STDEXEC::stdexec)
    get_target_property(_stdexec_aliased STDEXEC::stdexec ALIASED_TARGET)
    if(_stdexec_aliased)
        set(_stdexec_tgt ${_stdexec_aliased})
    else()
        set(_stdexec_tgt STDEXEC::stdexec)
    endif()
    get_target_property(_stdexec_incs ${_stdexec_tgt} INTERFACE_INCLUDE_DIRECTORIES)
    if(_stdexec_incs)
        set_property(TARGET ${_stdexec_tgt} APPEND PROPERTY
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${_stdexec_incs})
    endif()
    unset(_stdexec_incs)
    unset(_stdexec_tgt)
    unset(_stdexec_aliased)
endif()

# Tracy profiler (optional). Prefer vcpkg (`vcpkg install lightweight[tracy]`),
# fall back to CPM for non-vcpkg builds.
if(LIGHTWEIGHT_ENABLE_TRACY)
    find_package(Tracy CONFIG QUIET)
    if(Tracy_FOUND)
        message(STATUS "Tracy profiler found via find_package")
    else()
        message(STATUS "Tracy not found via find_package - downloading via CPM")
        set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
        set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
        CPMAddPackage(
            NAME tracy
            GITHUB_REPOSITORY wolfpld/tracy
            GIT_TAG v0.13.1
            EXCLUDE_FROM_ALL YES
            SYSTEM YES
        )
    endif()
endif()

# libzip support (Linux only)
if(NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(LIBZIP QUIET libzip)
    endif()

    if(LIBZIP_FOUND)
        message(STATUS "System libzip found: ${LIBZIP_VERSION}")
        # Create an imported interface target for system libzip so all targets can use `zip` consistently
        add_library(zip INTERFACE IMPORTED GLOBAL)
        target_include_directories(zip INTERFACE ${LIBZIP_INCLUDE_DIRS})
        target_link_libraries(zip INTERFACE ${LIBZIP_LINK_LIBRARIES})
        target_link_options(zip INTERFACE ${LIBZIP_LDFLAGS})
    else()
        message(STATUS "System libzip not found, downloading via CPM...")
        # Build options - disable crypto/compression features we don't need
        set(LIBZIP_OPTIONS
            "BUILD_SHARED_LIBS OFF"
            "BUILD_DOC OFF"
            "BUILD_EXAMPLES OFF"
            "BUILD_REGRESS OFF"
            "BUILD_TOOLS OFF"
            "BUILD_OSSFUZZ OFF"
            "ENABLE_OPENSSL OFF"
            "ENABLE_COMMONCRYPTO OFF"
            "ENABLE_GNUTLS OFF"
            "ENABLE_MBEDTLS OFF"
            "ENABLE_WINDOWS_CRYPTO OFF"
            "ENABLE_ZSTD OFF"
            "ENABLE_LZMA OFF"
        )
        CPMAddPackage(
            NAME libzip
            GITHUB_REPOSITORY nih-at/libzip
            GIT_TAG v1.11.3
            EXCLUDE_FROM_ALL YES
            OPTIONS ${LIBZIP_OPTIONS}
        )
        # Note: CPM-built libzip already creates a `zip` target (libzip::zip is an alias to it)
    endif()
endif()

# Detect clang-cl
set(CLANG_CL FALSE)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(CLANG_CL TRUE)
endif()

if(NOT WIN32 AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the build mode." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()

if(DEFINED MSVC)
    add_definitions(-D_USE_MATH_DEFINES)
    add_definitions(-DNOMINMAX)
    add_compile_options(/utf-8)
    add_compile_options(/nologo)
endif()

if(WIN32)
    # For Windows platform, we make sure that DLLs and EXEs are placed into the same directory.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/target CACHE STRING "")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/target CACHE STRING "")
endif()

enable_testing()

# Resolve Qt 6 for the optional migrations GUI before descending into tools.
# When LIGHTWEIGHT_BUILD_GUI=ON we auto-probe common install locations; if
# nothing is found we downgrade the option to OFF instead of failing the
# configure, so the rest of the project (library, dbtool, tests) still builds.
if(LIGHTWEIGHT_BUILD_GUI)
    include(LightweightFindQt)
    lightweight_probe_qt6(
        MIN_VERSION 6.8.0
        REQUIRED_COMPONENTS Core Gui Qml Quick QuickControls2
        RESULT_VAR _LIGHTWEIGHT_QT6_PROBED
    )
    if(NOT _LIGHTWEIGHT_QT6_PROBED)
        message(WARNING
            "LIGHTWEIGHT_BUILD_GUI=ON but Qt 6 could not be auto-detected.\n"
            "Pass -DQt6_DIR=<path-to-qt>/lib/cmake/Qt6 or add the Qt prefix to "
            "CMAKE_PREFIX_PATH to enable the GUI. Disabling GUI build for now. "
            "See cmake/LightweightFindQt.cmake for the list of probed install locations."
        )
        set(LIGHTWEIGHT_BUILD_GUI OFF CACHE BOOL "" FORCE)
    endif()
    unset(_LIGHTWEIGHT_QT6_PROBED)
endif()

add_subdirectory(src/Lightweight)

if(LIGHTWEIGHT_BUILD_TOOLS)
    add_subdirectory(src/tools)
endif()

if(LIGHTWEIGHT_BUILD_TESTS)
    add_subdirectory(src/tests)
endif()

if(LIGHTWEIGHT_BUILD_BENCHMARK)
    add_subdirectory(src/benchmark)
endif()

if(LIGHTWEIGHT_BUILD_EXAMPLES)
    add_subdirectory(src/examples)
endif()

if(LIGHTWEIGHT_BUILD_DOCUMENTATION)
    add_subdirectory(docs)
endif()

# Packaging (CPack: WIX on Windows, TGZ+DEB+RPM on Linux). Lives last so that
# every install(TARGETS ...) rule from the subdirectories above is in scope.
# Only relevant when Lightweight is the top-level project; when consumed via
# add_subdirectory the host project owns its own CPack configuration, and
# Lightweight's own CPACK_RESOURCE_FILE_* paths (rooted at ${CMAKE_SOURCE_DIR})
# would resolve into the host's tree.
if(PROJECT_IS_TOP_LEVEL)
    include(cmake/Packaging.cmake)
endif()
