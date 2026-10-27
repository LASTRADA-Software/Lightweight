# SPDX-License-Identifier: Apache-2.0
#
# Standalone consumer of the *installed* Lightweight package. It links only Lightweight::Profiles,
# which proves the public header is self-contained and nothing private leaks through the package.
# Built by cmake/tests/ProfilesPackageTest.cmake (CTest `ProfilesPackageTest`).

cmake_minimum_required(VERSION 3.25)
project(ProfilesConsumer CXX)

# Prefer a real reflection-cpp; the placeholder below only fills the gap when none is installed.
# The core package lists its header-only reflection-cpp dependency in Lightweight-targets.cmake but
# does not install or find it. Lightweight::Profiles does not use it, so a placeholder lets the
# package load without making this consumer depend on that dependency.
find_package(reflection-cpp QUIET)
if(NOT TARGET reflection-cpp::reflection-cpp)
    add_library(reflection-cpp::reflection-cpp INTERFACE IMPORTED)
endif()

find_package(Lightweight CONFIG REQUIRED)

add_executable(profiles_consumer main.cpp)
target_link_libraries(profiles_consumer PRIVATE Lightweight::Profiles)
