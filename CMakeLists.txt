cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

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

option(LIGHTWEIGHT_CXX26_REFLECTION "Enable experimental reflection support from C++26" OFF)

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
    CPMAddPackage("gh:contour-terminal/reflection-cpp#master")
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

if(NOT(CPACK_GENERATOR))
    set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_PACKAGE_NAME "Lightweight")
set(CPACK_PACKAGE_VENDOR "https://github.com/LASTRADA-Software/Lightweight")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight SQL C++ library on top of ODBC")
set(CPACK_PACKAGE_CONTACT "Christian Parpart <christian@parpart.family>")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
#set(CPACK_PACKAGE_EXECUTABLES LightweightTest "Lightweight Test Suite")
#set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")
#set(CPACK_PACKAGE_INSTALL_DIRECTORY "Lightweight SQL ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

enable_testing()

add_subdirectory(src/Lightweight)
add_subdirectory(src/tools)
add_subdirectory(src/tests)
add_subdirectory(src/benchmark)
add_subdirectory(src/examples)
add_subdirectory(docs)
