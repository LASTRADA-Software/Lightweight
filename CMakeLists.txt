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

find_package(reflection-cpp 0.4.0)
find_package(Catch2 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

option(LIGHTWEIGHT_CXX26_REFLECTION "Enable experimental reflection support from C++26" OFF)

if(LIGHTWEIGHT_CXX26_REFLECTION)
    add_definitions(-DLIGHTWEIGHT_CXX26_REFLECTION)
    set(CMAKE_CXX_STANDARD 26)
    message(STATUS "C++26 reflection support enabled.")
endif()



file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.8/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)


if(reflection-cpp_FOUND)
    message(STATUS "reflection-cpp found: ${reflection-cpp_INCLUDE_DIRS}")
else()
    message(STATUS "reflection-cpp not found, downloading...")
    CPMAddPackage("gh:contour-terminal/reflection-cpp#master")
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
