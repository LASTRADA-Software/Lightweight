cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

project(Lightweight VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

include(ClangTidy)
include(PedanticCompiler)

# download CPM.cmake
file(
    DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# TODO: use a release version of reflection-cpp as soon as possible
CPMAddPackage("gh:contour-terminal/reflection-cpp#master")

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
set(CPACK_PACKAGE_VENDOR "https://github.com/christianparpart/Lightwweight/")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight SQL C++ library on top of ODBC")
set(CPACK_PACKAGE_CONTACT "Christian Parpart <christian@parpart.family>")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
#set(CPACK_PACKAGE_EXECUTABLES LightweightTest "Lightweight Test Suite")
#set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")
#set(CPACK_PACKAGE_INSTALL_DIRECTORY "Lightweight SQL ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

add_subdirectory(src/Lightweight)
add_subdirectory(src/tools)

enable_testing()
add_subdirectory(src/tests)
add_subdirectory(src/benchmark)
add_subdirectory(src/examples)
add_subdirectory(docs)
